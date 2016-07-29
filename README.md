mangOH MQTT Client
==================

This repository implements an MQTT client for the [mangOH](http://mangoh.io).  The Legato app
MqttClient is a thin wrapper around the MQTT library provided by the
[Eclipse Paho project](https://www.eclipse.org/paho/).  The paho repository is referenced as a git
submodule.

Building
--------
Unfortunately there is currently no way to invoke an external make process from the Legato build
system, so the paho.mqtt.c code must be built before you can invoke mksys.  The makefile in the
MqttClient app folder will build the MqttClient library and the Paho library that it depends on.

Notes
-----
Unlike previous versions of MqttClient, this version does not invoke the dataConnectionService, so
calling apps must ensure that there is a live network connection.
