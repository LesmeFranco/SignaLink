# Carpeta de trabajo ESP32 - Bluetooth Low Energy (BLE)

En esta rama se trabajará con **BLE** en el ESP32.  
Encontrarás dos carpetas principales necesarias para el funcionamiento:

- **Gatt Client**
- **Gatt Server** 


---

## ⭐ GATT — ¿Qué es?

**GATT** (Generic ATTribute Profile) se encarga de definir la estructura de los datos expuestos por un dispositivo BLE. Hace uso de un protocolo de transferencia de datos denominado **ATT** (Protocolo de Atributos), utilizado para leer, escribir y notificar información.

Algo importante a tener en cuenta es que las conexiones GATT son **exclusivas**: un periférico BLE solo puede estar conectado a un dispositivo central a la vez. Una vez que el periférico se ha conectado, deja de anunciarse, impidiendo que otros puedan descubrir o conectarse hasta que se termine dicho vínculo.

Esta conexión proporciona un medio bidireccional de transferencia de datos, en el cual el dispositivo central puede leer o escribir información en el periférico, y el periférico puede notificar nuevos datos hacia el central.


---

## 🔗 Servicios

Un **servicio** se utiliza para organizar los datos en grupos lógicos.  
Cada servicio puede contener una o varias **características**.  
Además, cada servicio se identifica de forma única a partir de un **UUID** (de 16 o 128 bits).  
Este identificador puede ser:

- **De 16 bits** (para servicios BLE estandarizados).
- **De 128 bits** (para servicios personalizados específicos de tu aplicación).

---

## 🔗 Características

Una **característica** encapsula un único punto de datos (o un conjunto de ellos, por ejemplo, las lecturas X, Y y Z de un acelerómetro de 3 ejes).  
Al igual que los servicios, cada característica se identifica de forma única a partir de un **UUID** (de 16 o 128 bits).  
Puedes usar tanto las características estandarizadas por el **Bluetooth SIG** —garantizando así la interoperabilidad— como tus propias características personalizadas según tus necesidades.

---

## 📌 GATT SERVER

El GATT server es el encargado de **almacenar los datos** en forma de servicios y características, que estarán disponibles para que el GATT Client pueda leerlos, escribirlos o suscribirse a ellos.

---

## 📌 GATT CLIENT

El GATT client se encarga de **conectarse al GATT server** para descubrir sus servicios y características, leer sus valores o suscribirse para recibir notificaciones en el futuro.