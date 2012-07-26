#!/bin/sh

KEY="-"
HOST="127.0.0.1"
PORT="8181"
FEED="0"
TITLE="My Feed"

WEBSITE="http://www.wing-project.org/"
DISPOSITION="fixed"
NAME="Wing Wireless Mesh Network @ CREATE-NET"
LAT="51.5235375648154"
EXPOSURE="indoor"
LON="-0.0807666778564453"
DOMAIN="physical"
TAGS="[ \"arduino\", \"electricity\", \"energy\", \"power\", \"watts\" ]"

usage() {
	echo "Usage: $0 -h -t <title> -k <key> -a <host> -p <port> -f <feed> -t <lat> -n <lon>"
	exit 1
}	

while getopts "ht:k:a:p:f:t:n:" OPTVAL
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
	t) LAT="$OPTARG"
	  ;;
	n) LON="$OPTARG"
	  ;;
	h) usage
	  ;;
	esac
done

curl --request PUT \
     --data "{\"version\":\"1.0.0\", \"title\":\"$TITLE\", \"datastreams\":[], \"location\":{\"lat\":$LAT,\"lon\":$LON, \"disposition\":\"$DISPOSITION\", \"name\":\"$NAME\", \"exposure\":\"$EXPOSURE\", \"domain\":\"$DOMAIN\", \"tags\":$TAGS}}" \
     --header "X-PachubeApiKey: $KEY" \
     --verbose \
     http://$HOST:$PORT/feeds/$FEED

