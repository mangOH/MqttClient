mangOH MQTT Client
==================

This repository implements an MQTT client for the [mangOH](http://mangoh.io).  The implementation
is derived from the embedded C client provided by the
[Eclipse Paho project](https://www.eclipse.org/paho/).

Limitations
-----------
The API exposed by the MqttClient application is specifically tailored for use by the mangOH
[DataRouter](http://github.com/mangOH/DataRouter) in combination with
[AirVantage](http://airvantage.net) web services.  Unfortunately, this means that this application
is not very useful outside of that use case.

TODO
----
* Build an upstream version of paho rather than copying the source into this respository.
* Provide a general purpose MQTT Legato API that is not specific to AirVantage.  If an AirVantage
  layer is required, build that as well.
