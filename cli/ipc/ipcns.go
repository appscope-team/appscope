package ipc

import (
	"bufio"
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
	"syscall"

	"golang.org/x/sys/unix"
)

var (
	errGetProcStatus = errors.New("error getting process status")
	errGetProcGidMap = errors.New("error getting process gid map")
	errGetProcUidMap = errors.New("error getting process uid map")
)

// IpcNsIsSame compare own IPC namespace with specified pid
func IpcNsIsSame(pidCtx IpcPidCtx) (bool, error) {
	selfFi, err := os.Stat("/proc/self/ns/ipc")
	if err != nil {
		return false, err
	}

	pidFi, err := os.Stat(fmt.Sprintf("%v/proc/%v/ns/ipc", pidCtx.PrefixPath, pidCtx.Pid))
	if err != nil {
		return false, err
	}
	return os.SameFile(selfFi, pidFi), nil
}

// ipcNsSet sets current IPC namespace to the specified pid
func ipcNsSet(pidCtx IpcPidCtx) error {
	fd, err := os.Open(fmt.Sprintf("%v/proc/%v/ns/ipc", pidCtx.PrefixPath, pidCtx.Pid))
	if err != nil {
		return err
	}
	defer fd.Close()
	return unix.Setns(int(fd.Fd()), syscall.CLONE_NEWIPC)
}

// ipcNsRestore restore IPC namespace to the one specified in PID
func ipcNsRestore() error {
	fd, err := os.Open("/proc/self/ns/ipc")
	if err != nil {
		return err
	}
	defer fd.Close()
	return unix.Setns(int(fd.Fd()), syscall.CLONE_NEWIPC)
}

// IpcNsLastPidFromPid process the NsPid file for specified PID.
// Returns status if the specified PID residents in nested PID namespace, last PID in namespace and status of operation.
func IpcNsLastPidFromPid(pidCtx IpcPidCtx) (bool, int, error) {
	// TODO: goprocinfo does not support all the status parameters (NsPid)
	// handle procfs by ourselves ?
	file, err := os.Open(fmt.Sprintf("%v/proc/%v/status", pidCtx.PrefixPath, pidCtx.Pid))
	if err != nil {
		return false, -1, errGetProcStatus
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)
	scanner.Split(bufio.ScanLines)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "NSpid:") {
			var nsNestedStatus bool
			// Skip Nspid
			strPids := strings.Fields(line)[1:]

			strPidsSize := len(strPids)
			if strPidsSize > 1 {
				nsNestedStatus = true
			}
			nsLastPid, _ := strconv.Atoi(strPids[strPidsSize-1])

			return nsNestedStatus, nsLastPid, nil
		}
	}
	return false, -1, errGetProcStatus
}

// ipcNsTranslateUidFromPid translate specified uid to the ID-outside-ns based on specified pid.
// See https://man7.org/linux/man-pages/man7/user_namespaces.7.html for details
func ipcNsTranslateUidFromPid(uid int, pidCtx IpcPidCtx) (int, error) {
	file, err := os.Open(fmt.Sprintf("%v/proc/%v/uid_map", pidCtx.PrefixPath, pidCtx.Pid))
	if err != nil {
		return -1, errGetProcUidMap
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)
	scanner.Split(bufio.ScanLines)
	for scanner.Scan() {
		uidMapEntry := strings.Fields(scanner.Text())
		uidInsideNs, _ := strconv.Atoi(uidMapEntry[0])
		uidOutsideNs, _ := strconv.Atoi(uidMapEntry[1])
		length, _ := strconv.Atoi(uidMapEntry[2])
		if (uid >= uidInsideNs) && (uid < uidInsideNs+length) {
			return uidOutsideNs + uid, nil
		}
	}
	// unreachable
	return -1, errGetProcUidMap
}

// ipcNsTranslateGidFromPid translate specified gid to the ID-outside-ns based on specified pid.
// See https://man7.org/linux/man-pages/man7/user_namespaces.7.html for details
func ipcNsTranslateGidFromPid(gid int, pidCtx IpcPidCtx) (int, error) {
	file, err := os.Open(fmt.Sprintf("%v/proc/%v/gid_map", pidCtx.PrefixPath, pidCtx.Pid))
	if err != nil {
		return -1, errGetProcGidMap
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)
	scanner.Split(bufio.ScanLines)
	for scanner.Scan() {
		gidMapEntry := strings.Fields(scanner.Text())
		gidInsideNs, _ := strconv.Atoi(gidMapEntry[0])
		gidOutsideNs, _ := strconv.Atoi(gidMapEntry[1])
		length, _ := strconv.Atoi(gidMapEntry[2])
		if (gid >= gidInsideNs) && (gid < gidInsideNs+length) {
			return gidOutsideNs + gid, nil
		}
	}
	// unreachable
	return -1, errGetProcGidMap
}
