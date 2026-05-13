# shellcheck shell=sh
# Initialization script for bash, sh, mksh and ksh

case "$(basename $(readlink /proc/$$/exe))" in
*ksh*|zsh)
    alias which='alias | /usr/bin/which --tty-only --read-alias --show-tilde --show-dot'
    ;;
bash|sh)
    alias which='(alias; declare -f) | /usr/bin/which --tty-only --read-alias --read-functions --show-tilde --show-dot'
    ;;
*)
    ;;
esac
