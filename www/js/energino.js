google.load('visualization', '1', {packages: ['gauge']});

google.setOnLoadCallback(initialize);

var options = {
    width: 155,
    height: 155,
    redFrom: 9,
    redTo: 10,
    yellowFrom: 7.5,
    yellowTo: 9,
    min: 0,
    max: 10,
};

var gaugesDict = {};

function initialize() {
    var intervalID = setInterval(refresh, 8000);
    $.ajax({
        url: url + "/v2/feeds.json?user=wing",
        type: 'GET',
        dataType: 'json',
        cache: false,
        success: function (data) {
            plot(data)
        },
        error: function () {
            alert('boo!');
        },
        beforeSend: setHeader
    });
    function setHeader(xhr) {
        xhr.setRequestHeader('X-PachubeApiKey', key);
    }
}

function refresh() {
    $.ajax({
        url: url + "/v2/feeds.json?user=wing",
        type: 'GET',
        dataType: 'json',
        cache: false,
        success: function (data) {
            plot(data)
        },
        beforeSend: setHeader
    });
    function setHeader(xhr) {
        xhr.setRequestHeader('X-PachubeApiKey', key);
    }
}

function plot(data) {
    var len = Object.keys(gaugesDict).length
    if (len == 0) {
        for (node in data['results']) {
            var id = data['results'][node]['id']
            document.getElementById('feeds').innerHTML += '<tr><td width="20%"><img id="ap_status_' + id + '" width="70" src="imgs/ap_on.jpg"><p class="info">Feed: ' + id + ' (<a href="/feeds/'+id+'">view</a>)<br />Dispatcher: <span id="chart_dispatcher_' + id + '"></span><br />Energino: <span id="chart_energino_' + id + '"></span></td><td width="20%"><div class="charts" id="chart' + id + '"></div></td><td width="60%"><span id="chart_clients_'+id+'">n.a.</span></div></td></tr>';
        }
    }
    for (node in data['results']) {
        var id = data['results'][node]['id']
        var amperes = "0.0"
        var dispatcher = "n.a."
        var energino = "n.a."
        var switch_energino = 1
        if ('dispatcher' in data['results'][node]) {
            dispatcher = data['results'][node]['dispatcher']
        } 
        if ('energino' in data['results'][node]) {
            energino = data['results'][node]['energino']
        } 
        for (datastream in data['results'][node]['datastreams']) {
            if (data['results'][node]['datastreams'][datastream]['id'] == 'watts') {
                amperes = data['results'][node]['datastreams'][datastream]['current_value']
            }
            if (data['results'][node]['datastreams'][datastream]['id'] == 'switch') {
                switch_energino = data['results'][node]['datastreams'][datastream]['current_value']
            }
        }
	document.getElementById("chart_dispatcher_" + id).innerHTML = dispatcher
	if (energino != 'n.a.') { 
		document.getElementById("chart_energino_" + id).innerHTML = '<a href="http://'+energino+':8180/read/datastreams">' + energino + '</a>'
	} else {
		document.getElementById("chart_energino_" + id).innerHTML = energino
	}
        if ('clients' in data['results'][node]) {
		document.getElementById("ap_status_" + id).src = "imgs/ap_on.png"
		var clientsList = '<table width="100%"><tr>'
		if (data['results'][node]['clients'].length == 0) {	
			clientsList += '<td><p class="info">No clients</p></td>'
		}
		for (client in data['results'][node]['clients']) {
			ssid =  data['results'][node]['clients'][client]['ssid']
			mac =  data['results'][node]['clients'][client]['mac']
			clientsList += '<td><p class="info"><img width="60" src="imgs/sta.png" /><br/>'+ssid+'<br />'+mac+'</p></td>'
		}
		clientsList += "</tr></table>"
		document.getElementById("chart_clients_" + id).innerHTML = clientsList
	} else {
		document.getElementById("ap_status_" + id).src = "imgs/ap_off.png"
		document.getElementById("chart_clients_" + id).innerHTML = '<p class="info">No clients</p>'
	}
	if (parseFloat(amperes) < 0) {
		amperes = "0.0"
        }
        var table = google.visualization.arrayToDataTable([
            ['Label', 'Value'],
            ['Power [W]', parseFloat(amperes)]
        ]);
        if (!(id in gaugesDict)) {
            gaugesDict[id] = new google.visualization.Gauge(document.getElementById("chart" + id));
        }
        gaugesDict[id].draw(table, options);
    }
}
