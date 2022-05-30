
function cr() {
        echo -e "\033[1;32m$ $@\033[0m"
        bash -c "$@"
}