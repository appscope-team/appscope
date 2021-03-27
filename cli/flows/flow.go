package flows

import (
	"errors"
	"fmt"
	"hash/fnv"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/criblio/scope/events"
	"github.com/criblio/scope/util"
	"github.com/mitchellh/mapstructure"
)

type Flow struct {
	Hash          int64         `json:"hash"`
	HostIP        string        `json:"net.host.ip" mapstructure:"net.host.ip"`
	HostPort      int           `json:"net.host.port" mapstructure:"net.host.port"`
	PeerIP        string        `json:"net.peer.ip" mapstructure:"net.peer.ip"`
	PeerPort      int           `json:"net.peer.port" mapstructure:"net.peer.port"`
	Pid           int           `json:"pid"`
	InFile        string        `json:"in_file"`
	OutFile       string        `json:"out_file"`
	BaseDir       string        `json:"basedir"`
	StartTime     time.Time     `json:"start_time"`
	Duration      time.Duration `json:"duration" mapstructure:"duration"`
	LastSentTime  time.Time     `json:"last_sent_time"`
	BytesReceived int           `json:"net.bytes_recv" mapstructure:"net.bytes_recv"`
	BytesSent     int           `json:"net.bytes_sent" mapstructure:"net.bytes_sent"`
	Proc          string        `json:"proc"`
	Protocol      string        `json:"net.protocol" mapstructure:"net.protocol"`
	Transport     string        `json:"net.transport" mapstructure:"net.transport"`
	CloseReason   string        `json:"net.close.reason" mapstructure:"net.close.reason"`
}

func (f Flow) FlowFilePrefix() string {
	return fmt.Sprintf("%d_%s:%d_%s:%d", f.Pid, f.PeerIP, f.PeerPort, f.HostIP, f.HostPort)
}

func (f Flow) getHash() int64 {
	h := fnv.New32a()
	h.Write([]byte(f.FlowFilePrefix()))
	return int64(h.Sum32())
}

type FlowMap map[int64]Flow

func (fm FlowMap) List() []Flow {
	ret := []Flow{}
	check := FlowMap{}
	for _, f := range fm {
		if _, found := check[f.Hash]; !found {
			ret = append(ret, f)
			check[f.Hash] = f
		}
	}
	sort.Slice(ret, func(i, j int) bool { return ret[i].LastSentTime.Before(ret[j].LastSentTime) })
	return ret
}

func GetFlows(payloadPath string, eventsFile io.ReadSeeker) (FlowMap, error) {
	flows, err := getFlowFiles(payloadPath)
	if err != nil {
		return flows, err
	}
	eventFlows, err := getFlowEvents(eventsFile)
	if err != nil {
		return flows, err
	}
	for _, ef := range eventFlows.List() {
		if f, found := flows[ef.Hash]; found {
			f.mergeEventFlow(ef)
			flows[ef.Hash] = f
		} else {
			flows[ef.Hash] = ef
		}
	}
	return flows, nil
}

func getFlowFiles(path string) (FlowMap, error) {
	ret := FlowMap{}
	err := filepath.Walk(path, func(walkpath string, info os.FileInfo, err error) error {
		if info.IsDir() {
			return nil
		}
		if strings.Contains(walkpath, "af_int_err") {
			return nil
		}
		flow, err := parseFlowFileName(filepath.Base(walkpath))
		if err != nil {
			return err
		}
		flow.BaseDir, err = filepath.Abs(path)
		if err != nil {
			return err
		}
		stat := info.Sys().(*syscall.Stat_t)
		ctime := time.Unix(int64(stat.Ctim.Sec), int64(stat.Ctim.Nsec))
		flow.StartTime = ctime
		flow.LastSentTime = info.ModTime()
		flow.Duration = flow.LastSentTime.Sub(flow.StartTime)
		flow.Hash = flow.getHash()
		if flow.InFile != "" {
			flow.BytesReceived = int(info.Size())
		} else if flow.OutFile != "" {
			flow.BytesSent = int(info.Size())
		}
		if err != nil {
			return err
		}
		if f, found := ret[flow.Hash]; found {
			if flow.InFile != "" {
				f.InFile = flow.InFile
				f.BytesReceived = int(info.Size())
			}
			if flow.OutFile != "" {
				f.OutFile = flow.OutFile
				f.BytesSent = int(info.Size())
			}
			ret[f.Hash] = f
		} else {
			ret[flow.Hash] = flow
		}
		return nil
	})
	if err != nil {
		return ret, fmt.Errorf("error walking flows directory %s: %v", path, err)
	}
	return ret, nil
}

func getFlowEvents(r io.ReadSeeker) (FlowMap, error) {
	em := events.EventMatch{
		Sources: []string{"net.conn.open", "net.conn.close"},
	}

	in := make(chan map[string]interface{})
	var readerr error
	go func() {
		err := em.Events(r, in)
		if err != nil {
			if strings.Contains(err.Error(), "Error searching for Offset: EOF") {
				err = errors.New("Empty event file.")
			}
			readerr = err
			close(in)
		}
	}()
	ret := FlowMap{}
	for e := range in {
		// Reshape ports to ints
		if _, found := e["data"].(map[string]interface{})["net.host.port"]; found {
			e["data"].(map[string]interface{})["net.host.port"], _ = strconv.Atoi(e["data"].(map[string]interface{})["net.host.port"].(string))
			e["data"].(map[string]interface{})["net.peer.port"], _ = strconv.Atoi(e["data"].(map[string]interface{})["net.peer.port"].(string))
		}
		f := Flow{}
		err := mapstructure.Decode(e["data"], &f)
		if err != nil {
			return ret, fmt.Errorf("error decoding event: %v", err)
		}
		timeFp := e["_time"].(float64)
		f.LastSentTime = util.ParseEventTime(timeFp)
		f.StartTime = f.LastSentTime.Add(f.Duration * -1)
		f.Proc = e["proc"].(string)
		f.Pid = int(e["pid"].(float64))
		f.Hash = f.getHash()
		if err != nil {
			return ret, fmt.Errorf("error hashing flow: %v", err)
		}
		if strings.HasPrefix(f.Transport, "IP") { // Ignore Unix.TCP and others for now
			ret[f.Hash] = f
		}
	}
	if readerr != nil {
		return ret, readerr
	}
	return ret, nil
}

func (f *Flow) mergeEventFlow(ef Flow) {
	if ef.Duration == time.Duration(0) && ef.Transport != "" {
		f.StartTime = ef.StartTime
		f.Duration = ef.Duration
		f.LastSentTime = ef.LastSentTime
		f.BytesReceived = ef.BytesReceived
		f.BytesSent = ef.BytesSent
		f.Proc = ef.Proc
		f.Protocol = ef.Protocol
		f.Transport = ef.Transport
		f.CloseReason = ef.CloseReason
	}
}

func parseFlowFileName(filename string) (Flow, error) {
	ret := Flow{}
	re := regexp.MustCompile(`(\d+)_([0-9\.]+):(\d+)_([0-9\.]+):(\d+).(in|out)`)
	parts := re.FindStringSubmatch(filename)
	if parts == nil {
		return ret, fmt.Errorf("error parsing filename: %s", filename)
	}
	var err error
	ret.Pid, err = strconv.Atoi(parts[1])
	if err != nil {
		return ret, err
	}
	ret.PeerIP = parts[2]
	ret.HostIP = parts[4]
	ret.HostPort, err = strconv.Atoi(parts[5])
	if err != nil {
		return ret, err
	}
	ret.PeerPort, err = strconv.Atoi(parts[3])
	if err != nil {
		return ret, err
	}
	if parts[6] == "in" {
		ret.InFile = filename
	} else {
		ret.OutFile = filename
	}
	return ret, nil
}
