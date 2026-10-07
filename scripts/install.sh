#!/bin/sh

set -eu

show_help() {
    cat <<'HELP'
Usage: ./scripts/install.sh [--add-to-path] [--prefix PATH]

Builds and installs lolctl. The default prefix is $HOME/.local.

Options:
  --add-to-path  Add the install bin directory to .zshrc or .bashrc when needed.
  --prefix PATH  Install under a different prefix.
  --help         Show this help.
HELP
}

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_directory=$(dirname -- "$script_directory")
install_prefix=${LOLCTL_INSTALL_PREFIX:-"${HOME}/.local"}
add_to_path=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        --add-to-path)
            add_to_path=true
            shift
            ;;
        --prefix)
            if [ "$#" -lt 2 ]; then
                echo "error: --prefix requires a path" >&2
                exit 2
            fi
            install_prefix=$2
            shift 2
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            show_help >&2
            exit 2
            ;;
    esac
done

build_directory="${project_directory}/build/install"

cmake -S "$project_directory" -B "$build_directory" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "$build_directory" --parallel
cmake --install "$build_directory" --prefix "$install_prefix"

bin_directory="${install_prefix}/bin"
installed_command="${bin_directory}/lolctl"

if [ ! -x "$installed_command" ]; then
    echo "error: installation did not produce ${installed_command}" >&2
    exit 1
fi

case ":${PATH}:" in
    *":${bin_directory}:"*) path_is_configured=true ;;
    *) path_is_configured=false ;;
esac

if [ "$path_is_configured" = false ] && [ "$add_to_path" = true ]; then
    shell_name=$(basename -- "${SHELL:-sh}")
    case "$shell_name" in
        zsh) shell_profile="${HOME}/.zshrc" ;;
        bash) shell_profile="${HOME}/.bashrc" ;;
        *)
            echo "Installed ${installed_command}"
            echo "Add ${bin_directory} to PATH in your ${shell_name} configuration."
            exit 0
            ;;
    esac

    marker="# LoL Team Analysis CLI"
    if ! grep -F "$marker" "$shell_profile" >/dev/null 2>&1; then
        {
            printf '\n%s\n' "$marker"
            printf 'export PATH="%s:$PATH"\n' "$bin_directory"
        } >> "$shell_profile"
    fi
    echo "Installed ${installed_command}"
    echo "Open a new terminal or run: source ${shell_profile}"
elif [ "$path_is_configured" = false ]; then
    echo "Installed ${installed_command}"
    echo "Add ${bin_directory} to PATH, or rerun with --add-to-path."
else
    echo "Installed lolctl to ${installed_command}"
    echo "Run: lolctl init"
fi
