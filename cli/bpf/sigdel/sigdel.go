package sigdel

import "C"

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target $GOARCH -cc clang gen_sigdel ./sigdel.bpf.c -- -I/usr/include/bpf -I.

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"os"

	"github.com/rs/zerolog/log"

	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/perf"
	"github.com/cilium/ebpf/ringbuf"
	"github.com/cilium/ebpf/rlimit"
	"golang.org/x/sys/unix"
)

type sigdel_data_t struct {
	Pid     uint32
	NsPid   uint32
	Sig     uint32
	Errno   uint32
	Code    uint32
	Uid     uint32
	Gid     uint32
	Handler uint64
	Flags   uint64
	Comm    [32]byte
}

type SigEvent struct {
	sigdel_data_t
	CPU int
}

func setlimit() error {
	if err := unix.Setrlimit(unix.RLIMIT_MEMLOCK, &unix.Rlimit{
		Cur: unix.RLIM_INFINITY,
		Max: unix.RLIM_INFINITY,
	}); err != nil {
		log.Error().Err(err)
		return errors.New(fmt.Sprintf("failed to set temporary rlimit: %v", err))
	}
	return nil
}

func Sigdel(sigEventChan chan SigEvent) error {
	setlimit()

	objs := gen_sigdelObjects{}

	loadGen_sigdelObjects(&objs, nil)

	/*
	 * Trace events/types are described in /sys/kernel/debug/tracing/events
	 * This must be consistent with the .maps section name in C code.
	 */
	lnk, err := link.Tracepoint("signal", "signal_deliver", objs.SigDeliver, nil)
	if err != nil {
		log.Error().Msgf("*** ERROR: Set Tracepoint ***")
		return err
	}

	rd, err := perf.NewReader(objs.Events, os.Getpagesize())
	if err != nil {
		log.Error().Err(err)
		return errors.New("reader err")
	}

	defer objs.SigDeliver.Close()
	defer objs.Events.Close()
	defer lnk.Close()

	for {
		ev, err := rd.Read()
		if err != nil {
			log.Error().Err(err)
			return errors.New("Read fail error")
		}

		if ev.LostSamples != 0 {
			log.Warn().Msgf("*** The perf event array buffer is full, dropped %d samples ****", ev.LostSamples)
			continue
		}

		b_arr := bytes.NewBuffer(ev.RawSample)

		var data sigdel_data_t
		if err := binary.Read(b_arr, binary.LittleEndian, &data); err != nil {
			log.Warn().Msgf("parsing perf event: %s", err)
			continue
		}

		sigEventChan <- SigEvent{
			data,
			ev.CPU,
		}
	}
}

type OomEvent struct {
	Pid  uint32
	Comm [16]byte
}

func OOMProc(oomEventChan chan<- OomEvent) error {
	// Allow the current process to lock memory for eBPF resources.
	if err := rlimit.RemoveMemlock(); err != nil {
		log.Error().Err(err)
		return err
	}

	objs := gen_sigdelObjects{}
	if err := loadGen_sigdelObjects(&objs, nil); err != nil {
		log.Error().Err(err)
		return err
	}
	defer objs.Close()

	kp, err := link.Kprobe("oom_kill_process", objs.KprobeOomKillProcess, nil)
	if err != nil {
		log.Error().Err(err)
		return err
	}
	defer kp.Close()

	rd, err := ringbuf.NewReader(objs.OomEvents)
	if err != nil {
		log.Error().Err(err)
		return err
	}

	defer rd.Close()
	for {
		record, err := rd.Read()
		if err != nil {
			if errors.Is(err, ringbuf.ErrClosed) {
				log.Error().Err(err)
				return err
			}
			continue
		}

		var event OomEvent
		if err := binary.Read(bytes.NewBuffer(record.RawSample), binary.LittleEndian, &event); err != nil {
			continue
		}

		oomEventChan <- event
	}
}
