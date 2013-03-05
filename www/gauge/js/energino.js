google.load('visualization', '1', {packages: ['gauge']});

google.setOnLoadCallback(initialize);

var options = {
    width: 165,
    height: 165,
    redFrom: 9,
    redTo: 10,
    yellowFrom: 7.5,
    yellowTo: 9,
    min: 0,
    max: 10,
};

var gaugesDict = {};

function initialize() {
    var intervalID = setInterval(refresh, 5000);
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
            document.getElementById('feeds').innerHTML += '<tr><td width="20%"><p class="info">Feed: ' + id + ' (<a href="/feeds/'+id+'">view</a>)</p><p class="info">Node: <span id="chart_title_' + id + '"></span><br />Dispatcher: <span id="chart_dispatcher_' + id + '"></span></td><td width="20%"><div class="charts" id="chart' + id + '"></div></td><td width="60%"><p class="clients"><span id="chart_clients_'+id+'">n.a.</span></p></div></td></tr>';
        }
    }
    for (node in data['results']) {
        var id = data['results'][node]['id']
        var amperes = "0.0"
        var nbClients = "n.a."
        var title = "n.a."
        var dispatcher = "n.a."
        if ('dispatcher' in data['results'][node]) {
            dispatcher = data['results'][node]['dispatcher']
        } 
        if ('title' in data['results'][node]) {
            title = data['results'][node]['title']
        } 
        for (datastream in data['results'][node]['datastreams']) {
            if (data['results'][node]['datastreams'][datastream]['id'] == 'watts') {
                amperes = data['results'][node]['datastreams'][datastream]['current_value']
            }
            if (data['results'][node]['datastreams'][datastream]['id'] == 'clients') {
                nbClients = data['results'][node]['datastreams'][datastream]['current_value']
            }
        }
	var aLink = '<a href="http://'+dispatcher+':8180/read/datastreams">' + dispatcher + '</a>'
	document.getElementById("chart_title_" + id).innerHTML = title
	document.getElementById("chart_clients_" + id).innerHTML = nbClients
	if (dispatcher != 'n.a.') { 
		document.getElementById("chart_dispatcher_" + id).innerHTML = aLink
	} else {
		document.getElementById("chart_dispatcher_" + id).innerHTML = dispatcher
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
