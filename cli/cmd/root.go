package cmd

import (
	"fmt"
	"os"
	"strings"

	"github.com/criblio/scope/internal"
	"github.com/criblio/scope/run"
	"github.com/spf13/cobra"
)

// RootCmd represents the base command when called without any subcommands
var RootCmd = &cobra.Command{
	Use:   "scope",
	Short: "Cribl AppScope Command Line Interface\n\nAppScope is a general-purpose observable application telemetry system.\n\nRunning `scope` with no subcommands will execute the `scope run` command.",
}

// Execute adds all child commands to the root command and sets flags appropriately.
// This is called by main.main(). It only needs to happen once to the rootCmd.
func Execute() {
	RootCmd.SilenceErrors = true
	if err := RootCmd.Execute(); err != nil {
		if strings.HasPrefix(err.Error(), "unknown command") {
			for _, cmd := range RootCmd.Commands() {
				if os.Args[1] == cmd.Name() || cmd.HasAlias(os.Args[1]) {
					cmd.PrintErrln("Error:", err.Error())
					cmd.PrintErrf("Run '%v --help' for usage.\n", cmd.CommandPath())
					os.Exit(1)
				}
			}

			// Disallow bad argument combinations (see Arg Matrix at top of file)
			passthrough, _ := RootCmd.Flags().GetBool("passthrough")

			// Passthrough is a constructor command
			// It's only included in flags for helper menu
			// We do not support the flag in the cli proper
			if passthrough {
				helpErrAndExit(RootCmd, "Cannot specify --passthrough")
			}

			// If not a known command, scope run by default
			internal.InitConfig()
			rc := run.Config{}
			rc.Run(os.Args[1:])
		}
		fmt.Println(err)
		os.Exit(1)
	}
}

func init() {
	// Constructor flags (for help only)
	//	RootCmd.Flags().BoolP("passthrough", "z", false, "Scope an application with current environment & no config.")
}
