#!/bin/sh

TITLE="My feed"
KEY="-"
HOST="127.0.0.1"
PORT="8181"

usage() {
	echo "Usage: $0 -h -t <title> -k <key> -a <host> -p <port>"
	exit 1
}	

while getopts "ht:k:a:p:" OPTVAL
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
	h) usage
	  ;;
	esac
done

curl --request POST \
     --data "{\"title\":\"$TITLE\", \"version\":\"1.0.0\"}" \
     --header "X-PachubeApiKey: $KEY" \
     --verbose \
     http://$HOST:$PORT/feeds

