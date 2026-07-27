#!/bin/sh
# msys-browse — Interactive .msys file browser using fzf
#
# Usage:
#   msys-browse <archive.msys>              — browse single archive
#   msys-browse --overlay a.msys,b.msys     — browse overlay
#   msys-browse <archive> <path>            — start in subdirectory
#
# Dependencies: fzf, msysctl (from same directory or $PATH)

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
MSYSCTL="${SCRIPT_DIR}/msysctl"
[ -x "$MSYSCTL" ] || MSYSCTL="$(command -v msysctl 2>/dev/null)"
[ -x "$MSYSCTL" ] || { echo "msysctl not found"; exit 1; }

# Parse args
OVERLAY=""
ARCHIVE=""
START_DIR=""
FZF_OPTS=""

if [ "$1" = "--overlay" ]; then
	shift
	OVERLAY="--overlay $1"
	ARCHIVE="$1"
	shift
else
	ARCHIVE="$1"
	shift
fi
[ -n "$2" ] && START_DIR="$2"

# Ensure fzf is available
command -v fzf >/dev/null 2>&1 || { echo "fzf not installed"; exit 1; }

# ── interactive browser ──
browse() {
	local cwd="${1:-}"
	local indent="${2:-}"

	# List children
	local listing
	if [ -n "$cwd" ]; then
		listing=$($MSYSCTL $OVERLAY ls "$ARCHIVE" "$cwd" 2>/dev/null)
	else
		listing=$($MSYSCTL $OVERLAY ls "$ARCHIVE" "" 2>/dev/null)
	fi

	[ -z "$listing" ] && return

	# Format for fzf: prepend .. for parent, dirs with /
	local fzf_input=""
	local IFS='
'
	for line in $listing; do
		case "$line" in
			d\ *) fzf_input="${fzf_input}${indent}${line#d }/\n" ;;
			\ \ *) fzf_input="${fzf_input}${indent}${line#  }\n" ;;
		esac
	done

	[ -z "$fzf_input" ] && return

	local preview_cmd
	if [ -n "$cwd" ]; then
		preview_cmd="$MSYSCTL $OVERLAY cat \"$ARCHIVE\" \"$cwd/{}\" 2>/dev/null | head -100"
	else
		preview_cmd="$MSYSCTL $OVERLAY cat \"$ARCHIVE\" \"{}\" 2>/dev/null | head -100"
	fi

	local selected
	selected=$(printf "%b" "$fzf_input" | fzf --prompt="msys:${cwd:-/}> " \
		--preview="$preview_cmd" \
		--preview-window=right:60%:wrap \
		--bind="enter:accept,esc:cancel" \
		--height=80% \
		--reverse)

	[ -z "$selected" ] && exit 0

	# Trim indent
	selected="${selected## }"

	# Check if directory (ends with /)
	case "$selected" in
		*/)
			local dir="${selected%/}"
			if [ -n "$cwd" ]; then
				browse "$cwd/$dir" "${indent}  "
			else
				browse "$dir" "  "
			fi
			;;
		*)
			# File selected — show content with bat or cat
			local fullpath="$cwd${cwd:+/}$selected"
			clear
			if command -v bat >/dev/null 2>&1; then
				$MSYSCTL $OVERLAY cat "$ARCHIVE" "$fullpath" 2>/dev/null | bat --paging=never -l txt
			else
				$MSYSCTL $OVERLAY cat "$ARCHIVE" "$fullpath" 2>/dev/null
			fi
			echo ""
			printf "Press ENTER to continue, q to quit... "
			read -r key
			[ "$key" = "q" ] && exit 0
			browse "$cwd" "$indent"
			;;
	esac
}

browse "$START_DIR" ""
