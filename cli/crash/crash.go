package crash

import "github.com/rs/zerolog/log"

// GenFiles creates the following files for a given pid
// - snapshot
// - info (where available)
// - coredump (where available)
// - cfg (where available)
// - backtrace (where available)
func GenFiles(pid, sig, errno uint32) error {

	// where are we putting this stuff
	// if session directory exists, write to sessiondir/snapshot/
	// if not, write to /tmp/appscope/pid/
	dir := "/tmp"

	// retrieve info file

	// retrieve coredump file

	// retrieve cfg file

	// retrieve backtrace file

	// create snapshot file
	if err := GenSnapshotFile(pid, dir); err != nil {
		log.Error().Err(err).Msgf("error generating snapshot file")
		return err
	}

	return nil
}

// GenSnapshotFile generates the snapshot file for a given pid
func GenSnapshotFile(pid uint32, dir string) error {

	// Source: self (mostly via eBPF)

	// Time of Snapshot
	// AppScope Cli Version
	// Signal number
	// Signal handler
	// Error number
	// Process Name
	// Process Arguments
	// PID, PPID
	// User ID/ Group ID

	// Source: /proc or linux

	// Username/ Groupname  	// ebpf later?
	// Environment Variables
	// Machine Arch
	// Kernel version

	// Source: namespace

	// Distro, Distro version
	// Hostname

	/* Maybe later:
	// JRE version (if java)
	// Go version (if go)
	// Static or Dynamically linked(?)
	// Application version(?)
	// Application and .so elf files
	// AppScope Log Output
	// scope ps output
	// scope history output
	// Container Impl (docker, podman...)
	// Container Name/Version?
	// SELinux or AppArmor enforcing?
	// Unix Capabilities... PTRACE?...
	// Namespace Id's
	// Network interface status?
	// ownership/permissions on pertinent files/unix sockets
	// dns on pertinent host names
	*/

	// Open and Write file to dir

	return nil
}
