// Define the overlay, derived from google.maps.OverlayView
function Label(opt_options) {
    // Initialization
    this.setValues(opt_options);
    // Label specific
    var span = this.span_ = document.createElement('span');
    span.style.cssText = 'position: relative; left: -50%; top: 10px; ' + 'white-space: nowrap; border: 1px solid blue; ' + 'padding: 2px; background-color: white';
    var div = this.div_ = document.createElement('div');
    div.appendChild(span);
    div.style.cssText = 'position: absolute; display: none';
};

Label.prototype = new google.maps.OverlayView;

// Implement onAdd
Label.prototype.onAdd = function () {
    var pane = this.getPanes().overlayLayer;
    pane.appendChild(this.div_);
    // Ensures the label is redrawn if the text or position is changed.
    var me = this;
    this.listeners_ = [
    google.maps.event.addListener(this, 'position_changed', function () {
        me.draw();
    }), google.maps.event.addListener(this, 'text_changed', function () {
        me.draw();
    })];
};

// Implement onRemove
Label.prototype.onRemove = function () {
    this.div_.parentNode.removeChild(this.div_);
    // Label is removed from the map, stop updating its position/text.
    for (var i = 0, I = this.listeners_.length; i < I; ++i) {
        google.maps.event.removeListener(this.listeners_[i]);
    }
};

// Implement draw
Label.prototype.draw = function () {
    var projection = this.getProjection();
    var position = projection.fromLatLngToDivPixel(this.get('position'));
    var div = this.div_;
    div.style.left = position.x + 'px';
    div.style.top = position.y + 'px';
    div.style.display = 'block';
    this.span_.innerHTML = this.get('text').toString();
};

var labelsDict = {};
var markersDict = {};
var relaysDict = {};
var map
var bounds

function trigger(feed, relay) {
    return function () {
        $.ajax({
            url: url + "/v2/feeds/" + feed + "/write/switch.json",
            dataType: 'json',
            data: "{ \"current_value\":\"" + relay + "\", \"id\":\"Switch\" }",
            type: "PUT",
            cache: false,
            success: function () {
                refresh();
            },
            beforeSend: setHeader
        });
    };
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
        success: function(data) { 
            plot(data)
        },
        beforeSend: setHeader
    });
    function setHeader(xhr) {
        xhr.setRequestHeader('X-PachubeApiKey', key);
    }
}

function plot(data) {
    for (node in data['results']) {
        var id = data['results'][node]['id']
	var dutyCycle = "100"
	var amperes = "0.0"
	var relay = -1 
        for (datastream in data['results'][node]['datastreams']) {
            if (data['results'][node]['datastreams'][datastream]['id'] == 'current') {
                amperes = data['results'][node]['datastreams'][datastream]['current_value']
            }
            if (data['results'][node]['datastreams'][datastream]['id'] == 'duty_cycle') {
                dutyCycle = data['results'][node]['datastreams'][datastream]['current_value']
            }
            if (data['results'][node]['datastreams'][datastream]['id'] == 'switch') {
                relay = data['results'][node]['datastreams'][datastream]['current_value']
            }
        }
	if (parseFloat(amperes) < 0) {
		amperes = "0.0"
        }
        var beach = locations[id]
        var feed = data['results'][node]['feed']
        var title = data['results'][node]['title']
        var latLng = new google.maps.LatLng(beach['lat'], beach['lon'])
        bounds.extend(latLng)
        if (relay == "1") {
            relay = "0"
            dutyCycle = "0"
            var image = new google.maps.MarkerImage('images/dd-start.png', new google.maps.Size(20, 34), new google.maps.Point(0, 0), new google.maps.Point(10, 34))
        } else if (relay == 0) {
            relay = "1"
            var image = new google.maps.MarkerImage('images/dd-end.png', new google.maps.Size(20, 34), new google.maps.Point(0, 0), new google.maps.Point(10, 34))
        } else {
            relay = "-1"
            var image = new google.maps.MarkerImage('images/marker.png', new google.maps.Size(20, 34), new google.maps.Point(0, 0), new google.maps.Point(10, 34))
        }
        var shadow = new google.maps.MarkerImage('images/shadow.png', new google.maps.Size(37, 32), new google.maps.Point(0, 0), new google.maps.Point(0, 32));
        var marker = new google.maps.Marker({
            position: latLng,
            draggable: false,
            icon: image,
            shadow: shadow,
            map: map
        });
	if (relay != "-1") {
            google.maps.event.addListener(marker, 'click', trigger(id, relay))
        }
        var label = new Label({
            map: map,
            text: title + ' (' + dutyCycle + '%)'
        });
	if (markersDict[id]) {
            markersDict[id].setMap(null)
            labelsDict[id].setMap(null)
        }
        markersDict[id] = marker
        labelsDict[id] = label
        relaysDict[id] = relay
        label.bindTo('position', marker, 'position')
    }
}

function initialize() {
    var intervalID = setInterval(refresh, 5000);
    var myOptions = {
        mapTypeId: google.maps.MapTypeId.ROADMAP
    };
    bounds = new google.maps.LatLngBounds()
    map = new google.maps.Map(document.getElementById("map_canvas"), myOptions);
    $.ajax({
        url: url + "/v2/feeds.json?user=wing",
        type: 'GET',
        dataType: 'json',
        cache: false,
        success: function(data) { 
            plot(data)
            map.fitBounds(bounds)
        },
        error: function() { alert('boo!'); },
        beforeSend: setHeader
    });
    function setHeader(xhr) {
        xhr.setRequestHeader('X-PachubeApiKey', key);
    }
}
