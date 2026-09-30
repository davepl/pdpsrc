#!/usr/bin/env bash

set -euo pipefail

PDP11_HOST=${PDP11_HOST:-192.168.1.26}
PDP11_USER=${PDP11_USER:-dave}
PDP11_REMOTE_DIR=${PDP11_REMOTE_DIR:-source/repos/pdpsrc/bsd/novi}
PDP11_RUN_TESTS=${PDP11_RUN_TESTS:-0}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

for command in curl expect telnet; do
	if ! command -v "$command" >/dev/null 2>&1; then
		echo "Required command not found: $command" >&2
		exit 1
	fi
done

if [[ -z ${PDP11_PASSWORD:-} ]]; then
	if [[ ! -t 0 ]]; then
		echo "Set PDP11_PASSWORD when running without a terminal." >&2
		exit 1
	fi
	read -r -s -p "Password for ${PDP11_USER}@${PDP11_HOST}: " PDP11_PASSWORD
	echo
fi
export PDP11_HOST PDP11_USER PDP11_PASSWORD PDP11_REMOTE_DIR PDP11_RUN_TESTS

netrc_file=$(mktemp "${TMPDIR:-/tmp}/novi-netrc.XXXXXX")
cleanup() {
	rm -f "$netrc_file"
}
trap cleanup EXIT HUP INT TERM
chmod 600 "$netrc_file"
printf 'machine %s login %s password %s\n' \
	"$PDP11_HOST" "$PDP11_USER" "$PDP11_PASSWORD" >"$netrc_file"

shopt -s nullglob
source_files=(
	"$SCRIPT_DIR"/Makefile
	"$SCRIPT_DIR"/README.md
	"$SCRIPT_DIR"/*.c
	"$SCRIPT_DIR"/*.h
)

if (( ${#source_files[@]} == 0 )); then
	echo "No source files found in $SCRIPT_DIR" >&2
	exit 1
fi

echo "Uploading ${#source_files[@]} files to ${PDP11_HOST}:${PDP11_REMOTE_DIR} via FTP..."
for source_file in "${source_files[@]}"; do
	if [[ ! -f $source_file ]]; then
		continue
	fi
	filename=${source_file##*/}
	echo "  $filename"
	curl --silent --show-error --fail --disable-epsv \
		--netrc-file "$netrc_file" \
		--upload-file "$source_file" \
		"ftp://${PDP11_HOST}/${PDP11_REMOTE_DIR}/${filename}"
done

echo "Building on ${PDP11_HOST} via telnet..."
expect <<'EXPECT'
set timeout 180
match_max 100000

set host $env(PDP11_HOST)
set user $env(PDP11_USER)
set password $env(PDP11_PASSWORD)
set remote_dir $env(PDP11_REMOTE_DIR)
set run_tests $env(PDP11_RUN_TESTS)

proc wait_for_prompt {} {
	expect {
		-re {[A-Za-z0-9_.-]+@[A-Za-z0-9_.-]+:[^\r\n]*> } { return }
		timeout { puts stderr "Timed out waiting for the remote shell prompt"; exit 1 }
		eof { puts stderr "Telnet connection closed unexpectedly"; exit 1 }
	}
}

proc run_remote {label command} {
	global timeout expect_out
	set result_pattern [format {__NOVI_%s_RC__:([0-9]+)} $label]
	send -- "$command\r"
	send -- "echo __NOVI_${label}_RC__:\$status\r"
	set old_timeout $timeout
	set timeout 600
	expect {
		-re $result_pattern {
			set rc $expect_out(1,string)
		}
		timeout {
			puts stderr "Timed out running remote command: $command"
			exit 1
		}
		eof {
			puts stderr "Telnet connection closed while running: $command"
			exit 1
		}
	}
	set timeout $old_timeout
	wait_for_prompt
	if {$rc != 0} {
		puts stderr "Remote command failed with status $rc: $command"
		exit $rc
	}
}

spawn telnet $host
expect {
	-re {login:} { send -- "$user\r" }
	timeout { puts stderr "Timed out waiting for the telnet login prompt"; exit 1 }
	eof { puts stderr "Telnet connection closed before login"; exit 1 }
}
expect {
	-re {Password:} { send -- "$password\r" }
	timeout { puts stderr "Timed out waiting for the password prompt"; exit 1 }
	eof { puts stderr "Telnet connection closed before authentication"; exit 1 }
}
expect {
	-re {(?i)login incorrect} { puts stderr "Telnet login was rejected"; exit 1 }
	-re {[A-Za-z0-9_.-]+@[A-Za-z0-9_.-]+:[^\r\n]*> } {}
	timeout { puts stderr "Timed out waiting for the remote shell"; exit 1 }
	eof { puts stderr "Telnet connection closed after authentication"; exit 1 }
}

run_remote CD "cd $remote_dir"
run_remote CLEAN "make clean"
run_remote BUILD "make"
if {$run_tests eq "1"} {
	run_remote TEST "make test"
}

send -- "exit\r"
expect eof
EXPECT

if [[ $PDP11_RUN_TESTS == 1 ]]; then
	echo "PDP-11 deployment, build, and tests completed successfully."
else
	echo "PDP-11 deployment and build completed successfully."
	echo "Set PDP11_RUN_TESTS=1 to include the long-running regression test."
fi
