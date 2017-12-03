#!/bin/bash

# Does NUM10CLIENTS cycles of rapidly connecting 10 clients to server and
# then runs NUMCYCLES cycles of the specified command sequence for each client
# Calls client-notty as the client.


# Sequence of commands to be sent to server:
# (Separate each command line with \n, exit automatically sent at end.)
clientcommands=$'pwd\ncd\npwd\nls -l'


if [[ $# != 2 ]]; then
    echo "testserver NUM10CLIENTS NUMCYCLES"
    exit 1
fi

scriptdir=$(dirname "$0")


function clientscript()
{
    IFS=$'\n'
    echo "unset HISTFILE"
    for d in $(seq 1 "$1"); do
        for cmd in $clientcommands; do
            echo $cmd
            sleep 1
        done
    done

    echo "exit"

    #Keep stdin open briefly:
    sleep 2

    exit
}


if [[ ! -x ./test/client-notty ]]; then
    echo "Error: must have client executable named: client-notty"
    exit 1
fi

if ! /sbin/lsof -i :4070 &> /dev/null; then
    echo "Error: server does not seem to be running"
    exit 1
fi

rm -f test/testerrors

# Run specified number of clients against server:
for (( i=1; i<="$1"; i++ )); do
    for (( j=1; j<=10; j++)); do 
        clientscript "$2" | "$scriptdir"/client-notty 127.0.0.1 2>> test/testerrors &
    done
    sleep 1
done

wait

echo "Done Testing!"

if [[ -s test/testerrors ]]; then
    echo "Error messages generated, see file: test/testerrors"
else
    rm -f test/testerrors
fi

exit 0

#EOF
