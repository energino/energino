google.load('visualization', '1', {
    packages: ['gauge']
});

google.setOnLoadCallback(initialize);

var options = {
    width: 150,
    height: 150,
    redFrom: 5,
    redTo: 6,
    yellowFrom: 4.5,
    yellowTo: 5,
    min: 0,
    max: 6,
};

var optionsDutyCycle = {
    width: 150,
    height: 150,
    min: 0,
    max: 100,
};

var gaugesDict = {};
var gaugesDutyCycleDict = {};

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
            var title = data['results'][node]['title']
            if ('dispatcher' in data['results'][node]) {
                var dispatcher = data['results'][node]['dispatcher']
            } else {
                var dispatcher = "n.a."
            }
            if ('energino' in data['results'][node]) {
                var energino = data['results'][node]['energino']
            } else {
                var energino = "n.a."
            }
            document.getElementById('feeds').innerHTML += '<tr><td><p>Feed: ' + id + ' (<a href="/feeds/'+id+'">view</a>)</p><p>Node: ' + title + '<br />Energino: <a href="http://'+energino+':8180/read/datastreams">' + energino + '</a><br />Dispatcher: ' + dispatcher + '<br />Clients: <span id="chart_clients_'+id+'">n.a.</span></p></td><td><div class="chart" id="chart' + id + '"></div></td><td><div class="chart" id="chart_dc' + id + '"></div></td></tr>';
        }
    }
    for (node in data['results']) {
        var id = data['results'][node]['id']
        var amperes = "0.0"
        var dutyCycle = "0.0"
        var nbClients = "n.a."
        for (datastream in data['results'][node]['datastreams']) {
            if (data['results'][node]['datastreams'][datastream]['id'] == 'watts') {
                amperes = data['results'][node]['datastreams'][datastream]['current_value']
            }
            if (data['results'][node]['datastreams'][datastream]['id'] == 'duty_cycle') {
                dutyCycle = data['results'][node]['datastreams'][datastream]['current_value']
            }
            if (data['results'][node]['datastreams'][datastream]['id'] == 'clients') {
                nbClients = data['results'][node]['datastreams'][datastream]['current_value']
            }
        }
	document.getElementById("chart_clients_" + id).innerHTML = nbClients
	if (parseFloat(amperes) < 0) {
		amperes = "0.0"
        }
        var table = google.visualization.arrayToDataTable([
            ['Label', 'Value'],
            ['Power [W]', parseFloat(amperes)]
        ]);
        var tableDutyCycle = google.visualization.arrayToDataTable([
            ['Label', 'Value'],
            ['Duty [%]', parseFloat(dutyCycle)]
        ]);
        var chart = new google.visualization.Gauge(document.getElementById("chart" + id));
        var chartDutyCycle = new google.visualization.Gauge(document.getElementById("chart_dc" + id));
        if (id in gaugesDict) {
            gaugesDict[id].draw(table, options);
            gaugesDutyCycleDict[id].draw(tableDutyCycle, optionsDutyCycle);
        } else {
            chart.draw(table, options);
            gaugesDict[id] = chart
            chartDutyCycle.draw(tableDutyCycle, optionsDutyCycle);
            gaugesDutyCycleDict[id] = chartDutyCycle
        }
    }
}
