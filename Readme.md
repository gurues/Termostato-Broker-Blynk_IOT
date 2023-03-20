# TERMOSTATO-BROKER

## ANTECEDENTES

Quería realizar un termostato para mis familiares y amigos que no disponen de un sistema domótico y que están un poco "verdes" con la tecnología. Debía ser lo más minimalista posible y barato.
___

![TermostatoBroker](/Ayuda/Fotos/TermostatoBroker.jpg)

## DESCRIPCIÓN DEL PROYECTO

### Material necesario

* 1 x Wemos D1 mini (microcontrolador) ~ 3€
* Sensor de temperatura I2C SHT30 D1 mini ~ 2€
* Pulsador de control D1 mini ~ 1€
* Placa pug&play wemos D1 para conectar el hardware ~ 1€
* 1 x Shelly 1 (relé) ~ 13€
* Tener instalada la app Blynk en el móvil
* Disponer de una impresora 3D para imprimir la caja del Termostato-Broker

El Termostato-Broker cuesta en total unos 20€.

[Tienda -> Lolin Wemos D1 mini y accesorios en AliExpress](https://lolin.es.aliexpress.com/store/1331105?spm=a2g0o.detail.100005.1.593228ebGFeWij)

[Tienda -> ShellySpain](https://shellyspain.com/rele-shelly-1.html)

El Termostato-Broker controlará de forma domótica la temperatura de una vivienda mediante el arranque/paro de la caldera. Para realizar esto se programará el dispositivo de la siguiente forma:

* En el Wemos D1 mini se instala un broker mqtt que se encarga enviar las ordenes al relé de control de la calefacción además de desarrollarse todo el código de control del termostato.
* El Shelly 1, relé de control de la caldera, se debe "tasmotizar" con la versión 7.2 para que sea compatible con el Broker mqtt del Wemos D1 mini.
* Para la configuración y control del termostato domótico se dispone de una app desarrollada en [Blink](https://blynk.io/).

___

### Funcionamiento Termostato-Broker

La aplicación de control del Termostato-Broker está realizada con Blynk y mediante esta interface se puede controlar y configurar el Termostato-Broker. Los pasos a seguir para crear la app son (importante ver fotos en la carpeta Ayuda/Fotos):

* Crear un "Template".
  
  * Definir el dispositivo hardware, su conexión, en este caso ESP8266, WIFI, y obtener dos parámetros necesarios para la configuración
    de nuestro dispositivo.
    Estas líneas de código tienen que ir en la parte superior del código de control del termostato-Broker (firnware).

  ```Arduino
    #define BLYNK_TEMPLATE_ID "XXXXXXXXX"
    #define BLYNK_DEVICE_NAME "TermostatoBroker"  // Este es el nombre que he dado yo al Template
  ```

  * Definir los "Datastreams". Son los canales de comunicación entre el dispositivo y la app (Blynk.Cloud).
    En este caso usaremos los pines virtuales del VO al V12 y los difiniremos para comunicar, mostrar datos y controles de
    nuestros termostato-Broker.
  * Definir un "Web Dasboard". Interface gráfica de control del dispositivo desde cualquier navegador web.
  
* Crear un nuevo "Device" a partir de un "Template", donde escogeremos el "Template" que hemos creado.
  * Obtener el "Token" para el control de ese dispositivo.

  ```Arduino
    #define BLYNK_AUTH_TOKEN "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"  
  ```

  * Definir un "Mobile Dasboard". Interface gráfica de control del dispositivo desde un teléfono móvil.

Una vez cargado el firware al Wemos D1 mini cuando se alimenta por primera vez el Termstato-Broker, crea un punto de acceso WIFI llamado "Termostato_Broker_AP" para que lo configures. Una vez conectado tu PC, Tablet o móvil a esta red WIFI debes ingresar en la página <http://192.168.4.1> y configurar tus datos (red WIFI y token Blink para conectarse a la app). El Termostato-Broker se debe configurar como IP fija para que el Broker no cambie de IP y así el relé Shlelly 1 se pueda conectar siempre. Una vez hecho esto solo tienes que esperar a que el relé se conecte al Broker lo cual lo podrás ver en la app de control del dispositivo.

### Dispone de los siguientes controles

* SETPOINT. Temperatura que se desea alcanzar en la vivienda.
* RELE. Indicación del estado del relé con la configuración y datos actuales.
* ESTADO. Indicación del estado de la comunicación entre el relé y el Termostato-Broker. Siempre debe estar en verde para que el control
  sea posible. Si la comunicación se rompe el relé pasará a OFF como seguridad, apagando la calefacción.
* TEMPERATURA Y HUMEDAD ACTUAL. Son los valores de temperatura y humedad que nos indica el STH30. También podría mostrar la temperatura
  y humedad de un dispositivo externo que esté conectado al Termostato-Broker por mqtt (esto se debe configurar). Si no hay un envió de
  datos con una periodicidad menor a 5 minutos la app mostrará "No Data", indicando una anomalía en el dispositivo.
* CALEFACCION MANUAL. Este es el interruptor de AUTO/OFF de la calefacción y dependiendo de la temperatura actual y del Setpoint el relé
  arrancará o no la calefacción.
* BANDA DE FUNCIONAMIENTO. Es el intervalo en ºC de funcionamiento mínimo de la calefacción. Este parámetro es configurable y por
  defecto es 1ºC. Por ejemplo si el Setpoint es 22ºc y la Banda de Funcionamiento es 1ºC la calefacción estará arrancada desde 21ºc a
  22ºC.
* AJUSTE TEMPERATURA. Este parámetro permite ajustar el valor de la temperatura una vez contrastada la temperatura leída por el SHT30
  con otro instrumento. Este parámetro es configurable y por defecto es -3ºC.
* PROGRAMADOR HORARIO 1, 2 Y 3. Se disponen de 3 programadores horarios para configurar el arranque de la calefacción. En cada uno de
  ellos se debe poner la hora de inicio y de fin de funcionamiento que se requiere o dejar sin valor.
* CALEFACCION PROGRAMADA. Mediante este interruptor se activa la programación de la caldera, siendo el horario de funcionamiento de la
  misma lo indicado en los Programadores 1, 2 y 3.
* TERMINAL. La app dispone de un terminal que muestra el estado del Termostato-Broker, del relé de la caldera y de cualquier otro
  dispositivo conectado al Broker mqtt. También dispone de una serie de órdenes para verificar el funcionamiento del dispositivo.
   (Actualmente el "Widget Terminal" está en desarrollo y pierde el histórico cada vez que se sale de la app o se actualiza)
  * Ordenes mediante terminal:
    * RESET_ALL -> Termostato-Broker configuración de fábrica.
    * RESET -> Reinicio Termostato-Broker.
    * UPDATE_OTA -> Permisivo para la actualización OTA-LOCAL.
    * RESET_RELE -> Reinicio relé Shelly 1.
    * ESTADO_RELE -> Muestra estado actual relé.
    * PROGRAMADOR -> Muestra las horas de los Programadores Horarios.
    * COMUNICACION_RELE -> Muestra/Oculta la comunicación periódica con el relé.
* El dispositivo permite la actualización por OTA desde Blynk, por lo que no es necesario estar conectado a la misma red para actualizar
  el firmware.

___

## "Tasmotizar" y configurar Shelly 1 (relé cáldera)

"Tasmotizar" y configurar Shelly 1 se realizará de la siguiente forma:  (importante ver fotos en la carpeta Ayuda/Fotos)

* Actualizar o asegurarnos que el Shelly 1 tiene el firmware Tasmota 7.2. Si no es así actualizarlo a esta versión.
* Configurar el Template de Tasmota para el Shelly 1.
* Configurar mqtt. Aquí pondremos la IP del Termostato-Broker, los topic necesarios, etc.
* Configurar regla. Se configurará "Rule 1" para que si el shelly 1 no está conectado al Broker MQTT el relé esté "OFF".

```Arduino
 rule1 on Mqtt#Disconnected do power1 off endon 
```

* Poner en hora el shelly 1 mediante [comandos Tasmota](https://domotuto.com/comandos-para-introducir-en-tasmota-por-consola/)
* Para que un dispositivo "Tasmotizado" cree de nuevo un AP para volver a configurarse se le debe quitar y poner alimentación 4 veces (para la [versiones actuales de Tasmota son 7 veces](https://tasmota.github.io/docs/Device-Recovery/#program-memory)).

___

Por último comentar que en el archivo main.cpp se puede configurar las salidas DEBUG por el puerto serie y el uso de un sensor de medición de temperatura y humedad externo comunicando los datos del sensor por mqtt.

* El valor de temperatura del sensor externo llegará al Termostato-Broker mediante mqtt en el topic mqtt "sensor_temp".
* El valor de humedad del sensor externo llegará al Termostato-Broker mediante mqtt en el topic mqtt "sensor_hum".

```Arduino

// Comentar para deshabilitar el Debug de Blynk
#define BLYNK_PRINT Serial   // Defines the object that is used for printing
#define BLYNK_DEBUG        // Optional, this enables more detailed prints

//  Comentar para anular del debug serie Termostato-Broker
#define ___DEBUG___

//  Comentar para anular del debug serie correspondiente al Programador horario Termostato-Broker
#define ___DEBUG2___

// Comentar si no se dispone de sensor SHT30 y el sensor de control es externo mediante mqtt
#define sensor_sht30
```

Es importante recordar que en la parte superior del código del archivo main.cpp deben aparecer las siguientes instrucciones obtenidas al crear la app en Blynk

```Arduino
    #define BLYNK_TEMPLATE_ID "XXXXXXXXX"
    #define BLYNK_DEVICE_NAME "TermostatoBroker"  // Este es el nombre que he dado yo al Template
```

En la carpeta "AYUDA" de este proyecto se comparten fotos, diseños 3D, firmware Tasmota y archivo configuración del shelly_1 para la realización y configuración del Termostato-Broker.
___

## **Actualizado debido al cambio de política de BLYNK**

**Blynk modificó su política y redujo a 10 el número máximo de "Datastreams"**, canales de comunicación entre el dispositivo y la app (Blynk.Cloud). En la nueva versión del firm del Termostato-Broker 1.0.2, algunas de las funciones que antes se configuraban con "Datastreams" en la app, ahora se configuran con ordenes mediante el terminal de la app.

* Ordenes NUEVAS mediante terminal:
  * AJUSTE -> Muestra el ajuste de temperatura configurado.
  * AJUSTE= -> Configura ajuste de temperatura ej: AJUSTE=-2.
  * BANDA -> Muestra la banda de funcionamiento, configurado.
  * BANDA= -> Configura banda de funcionamiento, ej: BANDA=1.

PROGRAMADOR HORARIO

Se ha reducido de 3 a 2 los programadores horarios disponibles para configurar el arranque/paro de la calefacción.
___

## Realizado por gurues ~ Eneno 2023 ~  Actualizado Marzo 2023 (gurues@3DO ~ ogurues@gmail.com)
