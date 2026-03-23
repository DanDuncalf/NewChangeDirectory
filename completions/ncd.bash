# NCD Bash completion script
# Source this file in your .bashrc: source /path/to/completions/ncd.bash

_ncd_completions()
{
    local cur="${COMP_WORDS[COMP_CWORD]}"

    # Skip completion for flags
    if [[ "$cur" == /* || "$cur" == -* ]]; then
        return
    fi

    # Call NCD's completion subcommand
    local IFS=$'\n'
    COMPREPLY=( $(NewChangeDirectory /agent complete "$cur" --limit 20 2>/dev/null) )
}

complete -F _ncd_completions ncd
