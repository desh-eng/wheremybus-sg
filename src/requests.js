var REQ_TYPE = 'GET';
var EARTH_RADIUS_KM = 6371.009;
var MIN_STOPS = 1;
var MIN_RADIUS = 250;
var INCREASE_RADIUS = 250;
var MAX_RADIUS = 50000/2;
var BUS_STOPS = 'https://raw.githubusercontent.com/desh-eng/wmb_data/master/BusStop.json';
var BUS_ARRIVAL = 'http://datamall2.mytransport.sg/ltaodataservice/BusArrival?BusStopID=';
var MAX_SIZE = 41 - 1; // -1 to account for the "SHOW MORE" item
var BUS_ROUTES = 'https://raw.githubusercontent.com/desh-eng/wmb_data/master/bus-services/SERVICE.json';
var ALERT_DISTANCE = 1000;
var NOT_IN_OP = 'Not In Operation';
var OFFSET_SPECIAL = 6000;
var OFFSET_NITEOWL = 7000;
var OFFSET_NIGHTRIDER = 8000;
var OFFSET_NOT_IN_OP = 9000;

var INDEX_DESCRIPTION = 100;
var INDEX_DETAILS = 200;
var INDEX_SERVICE = 300;
var INDEX_ARRIVAL = 400;

var currentLat;
var currentLng;
var radius = 250;
var minStops = MIN_STOPS;
var nearbyBusStops = [];
var busArrivals = [];
var stopNo;
var alertDistance = ALERT_DISTANCE;
var remainingRoute = [];
var remainingRouteStops = [];
var indexNo = -1;
var watchId = null;

function xhrRequest(url, callback, async, headers) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function() { callback(JSON.parse(this.responseText)); };
    xhr.open(REQ_TYPE, url, async);
    if (headers) {
        xhr.setRequestHeader('AccountKey', 'k2gSGrJUEBh6quMh+5Npjg==');
        xhr.setRequestHeader('UniqueUserId', '56b0365b-43ad-44d8-bb29-a378445840f1');
        xhr.setRequestHeader('accept', 'application/json');
    }
    xhr.send();
}

function toRad(deg) {
    return (Math.PI/180)*deg;
}

function getDistance(lat1, lng1, lat2, lng2) {
    var latD = toRad(lat2) - toRad(lat1);
    var lngD = toRad(lng2) - toRad(lng1);
    var latD2 = latD*latD;
    var latMean = (toRad(lat1)+toRad(lat2)) / 2;
    var lngD2 = Math.pow(Math.cos(latMean)*lngD, 2);
    var distance = EARTH_RADIUS_KM * Math.sqrt(latD2+lngD2);
    
    // Returns in meters
    return Math.round(distance*1000);
}

function sortStops() {
    // console.log('Sorting bus stops...');
    
    function compare(a,b) {
        if (a.distance < b.distance) {
            return -1;
        } else if (a.distance > b.distance) {
            return 1;
        } else {
            return 0;
        }
    }
    
    nearbyBusStops.sort(compare);
}

function requestBusStops(data) {
    var start = new Date();
    // console.log('Bus Stops JSON Retrieved!');
    
    // console.log('Finding at least ' + minStops + ' bus stop(s) within ' + radius + 'm ...');
    nearbyBusStops = [];
    for (var i=0; i<data.features.length; i++) {
        var stopNo = data.features[i].properties.BUS_STOP_N;
        var roofNo = data.features[i].properties.BUS_ROOF_N;
        var description = data.features[i].properties.LOC_DESC;
        var lng = data.features[i].geometry.coordinates[0];
        var lat = data.features[i].geometry.coordinates[1];
        var distance = getDistance(currentLat, currentLng, lat, lng);

        if (distance <= radius ) {
            var busStop = {};
            busStop.stopNo = stopNo;
            busStop.roofNo = roofNo;
            busStop.description = description;
            busStop.distance = distance;

            nearbyBusStops.push(busStop);
        }
    }
    
    if (nearbyBusStops.length < minStops && radius < MAX_RADIUS) {
        // console.log('Increasing radius');
        radius += INCREASE_RADIUS;
        requestBusStops(data);
        return;
    }
    
    sortStops();
    
    var dictionary = {};
    if (nearbyBusStops.length > MAX_SIZE) {
        dictionary.KEY_BUS_STOPS = MAX_SIZE;
    } else {
        dictionary.KEY_BUS_STOPS = nearbyBusStops.length;
    }
    
    for (var j=0; j<dictionary.KEY_BUS_STOPS; j++) {
        var descrp = String(nearbyBusStops[j].description);
        if (descrp == "null") { descrp = 'Stop No: ' + nearbyBusStops[j].stopNo; }
        dictionary[j+INDEX_DESCRIPTION] = descrp;
        var details = nearbyBusStops[j].distance + 'm' + ' / ' + nearbyBusStops[j].stopNo +
            ' / ' + nearbyBusStops[j].roofNo;
        dictionary[j+INDEX_DETAILS] = details;
        // console.log(dictionary[j+INDEX_DESCRIPTION] + ' | ' + dictionary[j+INDEX_DETAILS]);
    }
    
    if (dictionary.KEY_BUS_STOPS === 0) {
        dictionary[dictionary.KEY_BUS_STOPS + INDEX_DESCRIPTION] = 'NO BUS SERVICE';
        dictionary[dictionary.KEY_BUS_STOPS + INDEX_DETAILS] = 'No bus service available';
        dictionary.KEY_BUS_STOPS++;
    } else if (dictionary.KEY_BUS_STOPS < MAX_SIZE) {
        dictionary[dictionary.KEY_BUS_STOPS + INDEX_DESCRIPTION] = 'DISPLAY MORE';
        dictionary[dictionary.KEY_BUS_STOPS + INDEX_DETAILS] = 'Show more bus stops';
        dictionary.KEY_BUS_STOPS++;
    }
    
    // console.log('Sending bus stops...');
//     // console.log(JSON.stringify(dictionary));
    Pebble.sendAppMessage(dictionary);
    
    var end = new Date();
    // console.log('Time to request nearby bus stops: ' + (end-start) + 'ms');
}

var locationOptions = {
    enableHighAccuracy: true,
//     maximumAge: 10000,
    maximumAge: 2500,
    timeout: 10000
};

function locationSuccess(pos) {
    // console.log('Location Success!');
    radius = MIN_RADIUS;
    currentLat = pos.coords.latitude;
    currentLng = pos.coords.longitude;
    nearbyBusStops = [];
    
    // 680423
//     currentLat = 1.38203301;
//     currentLng = 103.7402850103;
    // Orchard Road
//     currentLat = 1.3042920;
//     currentLng = 103.8324940;
    // Jalan Kayu
//     currentLat = 1.40231301;
//     currentLng = 103.8719490;
    // Queenstown
//     currentLat = 1.2976394;
//     currentLng = 103.8039897;
    // Jurong East
//     currentLat = 1.3343080;
//     currentLng = 103.7419580;
    // Woodlands
//     currentLat = 1.4367333;
//     currentLng = 103.7859559;
    
    // console.log('Retrieving Bus Stops...');
    xhrRequest(BUS_STOPS, requestBusStops, true, false);
}

function locationError(err) {
    // console.log('Location Error!');
}

function requestLocation(enhanced) {
    // console.log('Location requested.');
    if (enhanced) {
        // console.log('Retrieve more results');
        minStops = nearbyBusStops.length + 1;
    }
    navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
}

function sortArrivals() {
    // console.log('Sorting bus services...');
    
    function compare(a,b) {
        if (isNaN(b.number) || (a.number < b.number)) {
            return -1;
        } else if (isNaN(a.number) || (a.number > b.number)) {
            return 1;
        } else {
            return 0;
        }
    }
    
    busArrivals.sort(compare);
}

function requestArrival(data) {
    var start = new Date();
    // console.log('Bus Arrivals Retrieved!');
    
    // console.log('Formatting Arrivals...');
    // console.log(JSON.stringify(data));
            
    busArrivals = [];
    var exists = [];
    for (var i=0; i<data.Services.length; i++) {
        var bus = {};
        var operator = data.Services[i].Operator;
        var service = data.Services[i].ServiceNo;
        var arrival = data.Services[i].NextBus.EstimatedArrival;
        var arrival2 = data.Services[i].SubsequentBus.EstimatedArrival; 
        var operation = data.Services[i].Status.toLowerCase();
        var current = new Date();
        
//         if (arrival === arrival2 && arrival === null) {
//             continue;
//         }
        if (exists.indexOf(service) != -1) { continue; }
        else { exists.push(service); }

        var eta;
        if (arrival !== null) {
            arrival = new Date(data.Services[i].NextBus.EstimatedArrival);
            eta = Math.floor((arrival.getTime() - current.getTime())/(1000*60));
            if (eta <= 0) { 
                eta = 'Arriving'; 
            } else {
                eta += ' min';
            }
        } else {
            eta = 'NA';
        }
        
        var eta2;
        if (arrival2 !== null) {
            arrival2 = new Date(data.Services[i].SubsequentBus.EstimatedArrival);
            eta2 = Math.floor((arrival2.getTime() - current.getTime())/(1000*60));
            if (eta2 <= 0) { 
                eta2 = 'Arriving'; 
            } else {
                eta2 += ' min';
            }
        } else {
            eta2 = 'NA';
        }
        
        bus.number = parseInt(service);
        if (isNaN(bus.number) && service.indexOf('NR') === 0) {
            bus.number = parseInt(service.substring(2)) + OFFSET_NIGHTRIDER;
        } else if (bus.number < 10 && service.indexOf('N') === 1) {
            bus.number += OFFSET_NITEOWL;
        } else if (isNaN(bus.number)) {
            bus.number = parseInt(service.substring(2)) + OFFSET_SPECIAL;
        }
        bus.service = service + ' (' + operator + ')';
        bus.eta = eta + ' / ' + eta2;
        
        if (operation.localeCompare(NOT_IN_OP.toLowerCase()) === 0) {
            bus.eta = NOT_IN_OP;
            bus.number += OFFSET_NOT_IN_OP;
        }
        
//         if (bus.eta.localeCompare('NA / NA') === 0) {
//             bus.number += OFFSET_NOT_IN_OP;
//         }
        
        busArrivals.push(bus);
    }
    
    sortArrivals();
    
    if (busArrivals.length === 0) {
        var noBus = {};
        noBus.number = 0;
        noBus.service = 'NO BUS SERVICE';
        noBus.eta = 'No bus service available';
        busArrivals.push(noBus);
    }
    
    var dictionary = {};
    dictionary.KEY_BUS_SERVICES = busArrivals.length;
    // console.log(busArrivals.length);
    
    for (var j=0; j<busArrivals.length; j++) {
        var busService = busArrivals[j];
        // console.log(busService.service + ' ' + busService.eta);
        dictionary[j+INDEX_SERVICE] = busService.service;
        dictionary[j+INDEX_ARRIVAL] = busService.eta;
    }
        
    // console.log('Sending bus arrivals...');
//     // console.log(JSON.stringify(dictionary));
    Pebble.sendAppMessage(dictionary);
    
    var end = new Date();
    // console.log('Time to request arrival timings: ' + (end-start) + 'ms');
}

function requestBusStop(busStop) {
    // console.log('Requested Bus Stop: ' + busStop);
    var stopUrl = BUS_ARRIVAL + busStop;
    
    // console.log('Retrieving Bus Arrival...');
    xhrRequest(stopUrl, requestArrival, true, true);
}

function requestRouteNames(data) {
    var start = new Date();
    // console.log('Updating bus route names...');
    
    remainingRouteStops = [];
    for (var i=0; i<remainingRoute.length; i++) {
        var target = remainingRoute[i];
        for (var j=0; j<data.features.length; j++) {
            var stopNo = data.features[j].properties.BUS_STOP_N;
            if (stopNo.localeCompare(target) === 0) {
                var stopDesc = data.features[j].properties.LOC_DESC;
                if (stopDesc === null) { stopDesc = 'Stop No: ' + stopNo; }
                var stop = {};
                stop.stopNo = stopNo;
                stop.description = stopDesc;
                stop.lng = data.features[j].geometry.coordinates[0];
                stop.lat = data.features[j].geometry.coordinates[1];
                remainingRouteStops.push(stop);
                // console.log(JSON.stringify(stop));
                break;
            }
        }
    }
    
    var dictionary = {};
    dictionary.KEY_BUS_ROUTE = remainingRouteStops.length;
    
    for (var k=0; k<remainingRouteStops.length; k++) {
        dictionary[k+INDEX_DESCRIPTION] = remainingRouteStops[k].description;
    }
    
    if (remainingRouteStops.length === 0) {
        dictionary.KEY_BUS_ROUTE = remainingRouteStops.length+1;
        dictionary[INDEX_DESCRIPTION] = "NO ROUTE FOUND";
    }

    // console.log(JSON.stringify(dictionary));
    Pebble.sendAppMessage(dictionary);
    
    var end = new Date();
    // console.log('Time to update subsequent bus stops in route: ' + (end-start) + 'ms');
}

function requestRoute(data) {
    // console.log('Bus Route Retrieved!');
    // console.log(JSON.stringify(data));
    remainingRoute = [];
    var routeOne = data["1"].stops;
    var stopIndex = routeOne.indexOf(stopNo.toString());
    if (stopIndex != -1) {
        // console.log('Stop No ' + stopNo + ' @ [' + stopIndex + ']');
        for (var i=stopIndex; i<routeOne.length; i++) {
            remainingRoute.push(routeOne[i]);
        }
    } 
    
    if (stopIndex == -1 || remainingRoute.length == 1) {
        remainingRoute = [];
        var routeTwo = data["2"].stops;
        stopIndex = routeTwo.indexOf(stopNo.toString());
        if (stopIndex != -1) { 
            // console.log('Stop No ' + stopNo + '@ [' + stopIndex + ']');
            for (var j=stopIndex; j<routeTwo.length; j++) {
                remainingRoute.push(routeTwo[j]);
            }
        }
    }
    
    xhrRequest(BUS_STOPS, requestRouteNames, true, false);
}

function requestBusRoute(service) {
    // console.log('Requested Bus Service: ' + service);
    var routeUrl = BUS_ROUTES.replace("SERVICE", service);
    
    // console.log('Retrieving Bus Route...');
    xhrRequest(routeUrl, requestRoute, true, false);
}

function requestDistance(pos) {
    // console.log('Calculating distance to destination...');
    currentLat = pos.coords.latitude;
    currentLng = pos.coords.longitude;
    
    var lat = remainingRouteStops[indexNo].lat;
    var lng = remainingRouteStops[indexNo].lng;
    var description = remainingRouteStops[indexNo].description;    
    var distance = getDistance(currentLat, currentLng, lat, lng);
    
    var toAlert = 0;
    if (distance <= alertDistance) {
        toAlert = 1;
    }
    
    var summary = distance + 'm\nto reach\n' + description;
    
    var dictionary = {
        KEY_WAKE_BOOL: toAlert,
        KEY_WAKE_SUMMARY: summary,
    };
    
    // console.log(JSON.stringify(dictionary));
    Pebble.sendAppMessage(dictionary);
}

Pebble.addEventListener('ready',
    function(e) { requestLocation(0); }
);

Pebble.addEventListener('appmessage',
    function(e) { 
        var cmd = e.payload[45];
        var details = cmd.split(' / ');
        // console.log('AppMessage Received: ' + cmd); 
        
        if (details[0].toLowerCase() == "location") {
            requestLocation(0);
        } else if (details[0].toLowerCase() == "more") {
            requestLocation(1);
        } else if (details[0].toLowerCase() == "distance") {
            indexNo = parseInt(details[1]);
            if (isNaN(indexNo)) {
                navigator.geolocation.clearWatch(watchId);
                watchId = null;
                indexNo = null;
            } else {
                if (indexNo > 0) {
                    var lat1 = remainingRouteStops[indexNo].lat;
                    var lng1 = remainingRouteStops[indexNo].lng;
                    var lat2 = remainingRouteStops[indexNo-1].lat;
                    var lng2 = remainingRouteStops[indexNo-1].lng;
                    alertDistance = getDistance(lat1, lng1, lat2, lng2);
                    alertDistance = Math.floor(alertDistance/10) * 10;
                    alertDistance = Math.min(alertDistance, ALERT_DISTANCE);
                } else {
                    alertDistance = ALERT_DISTANCE;
                }
                if (watchId !== null) {
                    navigator.geolocation.clearWatch(watchId);
                }
                watchId = navigator.geolocation.watchPosition(requestDistance, locationError, locationOptions);
            }
        } else if (details.length == 3) {
            stopNo = details[1];
            requestBusStop(stopNo);
        } else {
            var serviceNo = details[0].split(' ')[0];
            requestBusRoute(serviceNo);
        }
    }                       
);