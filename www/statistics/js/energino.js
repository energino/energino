google.load("visualization", "1", {packages:["corechart"]});

google.setOnLoadCallback(initialize);

var createRingBuffer = function(length) {
  var pointer = 0, buffer = []; 
  return {
    push : function(item) {
      buffer[pointer] = item;
      pointer = (length + pointer + 1) % length;
    },
    toArray : function() {
      res = []
      for (i = 0; i < length; i++) {
	if (buffer[(length + pointer + i) % length]) {
          res.push(buffer[(length + pointer + i) % length]);
        }
      }
      return res;
    }
  };
};

var options = {
    width: 400,
    height: 160,
    hAxis : { 'title' : 'Time [s]' },
    vAxis : { 'title' : 'Power [W]', 'maxValue' : 8, 'minValue' : 0 },
    legend: {'position' : 'none'}
};

var optionsDutyCycle = {
    width: 400,
    height: 160,
    hAxis : { 'title' : 'Time [s]' },
    vAxis : { 'title' : 'Duty Cycle [%]', 'maxValue' : 110, 'minValue' : 0  },
    legend: {'position' : 'none'}
};

var statsDict = {};
var dataDict = {};

var statsDutyCycleDict = {};
var dataDutyCycleDict = {};

var init = 0;

var samples = 60;
var period = 5000;

function initialize() {
    var interalID = setInterval(refresh, period);
    init = new Date().getTime();
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
    var len = Object.keys(statsDict).length
    if (len == 0) {
        for (node in data['results']) {
            var id = data['results'][node]['id']
            document.getElementById('feeds').innerHTML += '<tr><td width="16%"><p class="info">Feed: ' + id + ' (<a href="/feeds/'+id+'">view</a>)</p><p class="info">Node: <span id="chart_title_' + id + '"></span><br />Energino: <span id="chart_energino_' + id + '"></span><br />Dispatcher: <span id="chart_dispatcher_' + id + '"></span><br />Clients: <span id="chart_clients_'+id+'">n.a.</span></p></td width="42%"><td><div id="chart_' + id + '"></div></td><td width="42%"><div id="chart_duty_cyle_' + id + '"></div></td></tr>';
        }
    }
    for (node in data['results']) {
        var id = data['results'][node]['id']
        var amperes = 0.0
        var dutyCycle = 100
        var nbClients = 0
        var energino = "n.a."
        var title = "n.a."
        var dispatcher = "n.a."
        if ('dispatcher' in data['results'][node]) {
            dispatcher = data['results'][node]['dispatcher']
        } 
        if ('title' in data['results'][node]) {
            title = data['results'][node]['title']
        } 
        if ('energino' in data['results'][node]) {
            energino = data['results'][node]['energino']
        } 
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

	var aLink = '<a href="http://'+energino+':8180/read/datastreams">' + energino + '</a>'

	document.getElementById("chart_energino_" + id).innerHTML = aLink
	document.getElementById("chart_title_" + id).innerHTML = title
	document.getElementById("chart_clients_" + id).innerHTML = nbClients
	document.getElementById("chart_dispatcher_" + id).innerHTML = dispatcher

	if (amperes < 0) {
		amperes = 0.0
        }

        if (!(id in dataDict)) {
          dataDict[id] = createRingBuffer(samples);
        }

        if (!(id in dataDutyCycleDict)) {
          dataDutyCycleDict[id] = createRingBuffer(samples);
        }

	var now = Math.floor((new Date().getTime() - init) / 1000);

        dataDict[id].push( [ now, parseFloat(amperes) ] )
        dataDutyCycleDict[id].push( [ now, parseInt(dutyCycle) ] )

	var tmpData = [ ['Time [s]', 'Power [W]'] ].concat(dataDict[id].toArray())
	var dataStruct = google.visualization.arrayToDataTable(tmpData);

	var tmpDutyCycleData = [ ['Time [s]', 'Duty Cycle [%]'] ].concat(dataDutyCycleDict[id].toArray())
	var dataDutyCycleStruct = google.visualization.arrayToDataTable(tmpDutyCycleData);

        if (!(id in statsDict)) {
            statsDict[id] = new google.visualization.LineChart(document.getElementById('chart_' + id));
        }

        if (!(id in statsDutyCycleDict)) {
            statsDutyCycleDict[id] = new google.visualization.LineChart(document.getElementById('chart_duty_cyle_' + id));
        }

        statsDict[id].draw(dataStruct, options);
        statsDutyCycleDict[id].draw(dataDutyCycleStruct, optionsDutyCycle);

    }
}
