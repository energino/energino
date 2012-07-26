#!/bin/sh

TITLE="My feed"
KEY="-"
HOST="127.0.0.1"
PORT="8181"
FEED="0"

usage() {
	echo "Usage: $0 -h -t <title> -k <key> -a <host> -p <port> -f <feed>"
	exit 1
}	

while getopts "ht:k:a:p:f:" OPTVAL
do
	case $OPTVAL in
	t) TITLE="$OPTARG"
	  ;;
	k) KEY="$OPTARG"
	  ;;
	a) HOST="$OPTARG"
	  ;;
	p) PORT="$OPTARG"
	  ;;
	f) FEED="$OPTARG"
	  ;;
	h) usage
	  ;;
	esac
done

curl --request DELETE \
     --header "X-PachubeApiKey: $KEY" \
     --verbose \
     http://$HOST:$PORT/feeds/$FEED

