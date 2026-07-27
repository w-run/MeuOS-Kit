#!/bin/bash
# msysctl-completion.bash — bash autocompletion for msysctl
# Source this file: source msysctl-completion.bash

_msysctl()
{
	local cur prev words cword
	_init_completion || return

	# List of commands
	local cmds="cat ls find tree extract info verify stat grep hist"

	# First word after msysctl or --overlay
	local cmd_idx=1
	if [ "$1" = "msysctl" ]; then
		cmd_idx=2
	fi

	# Check if current word is a command
	if [ $cword -eq $cmd_idx ]; then
		COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
		return
	fi

	# Handle --overlay
	if [ "$prev" = "--overlay" ]; then
		COMPREPLY=($(compgen -f -X '!*msys' -- "$cur"))
		return
	fi

	# Archive argument: complete .msys files
	if [ $cword -eq $((cmd_idx + 1)) ]; then
		COMPREPLY=($(compgen -f -X '!*msys' -- "$cur"))
		return
	fi

	# hist subcommands
	if [ "$3" = "hist" ]; then
		local hist_cmds="add list cat diff"
		if [ $cword -eq $((cmd_idx + 1)) ]; then
			COMPREPLY=($(compgen -W "$hist_cmds" -- "$cur"))
			return
		fi
		# hist add/list: archive then path
		if [ $cword -eq $((cmd_idx + 2)) ]; then
			COMPREPLY=($(compgen -f -X '!*msys' -- "$cur"))
			return
		fi
	fi

	# For cat, stat, grep: complete paths within archive
	local cmd="$3"
	if [ -z "$cmd" ]; then
		cmd="$1"
	fi
	case "$cmd" in
		cat|stat|grep)
			local archive=""
			if [ "$1" = "--overlay" ]; then
				archive="$2"
			else
				archive="$2"
			fi
			if [ -f "$archive" ] && [ $cword -ge $((cmd_idx + 2)) ]; then
				# Use msysctl find to list files
				local files=$(${COMP_WORDS[0]} find "$archive" "" 2>/dev/null | head -100)
				COMPREPLY=($(compgen -W "$files" -- "$cur"))
				return
			fi
			;;
	esac

	# Default: file completion
	COMPREPLY=($(compgen -f -- "$cur"))
}

complete -F _msysctl msysctl
