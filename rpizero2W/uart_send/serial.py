import serial

# Cambia '/dev/serial0' si usas otro puerto
ser = serial.Serial('/dev/serial0', 115200, timeout=1)

print("Esperando datos por UART...")

try:
    while True:
        if ser.in_waiting > 0:
            data = ser.readline().decode('utf-8').strip()
            print(f"Recibido: {data}")
except KeyboardInterrupt:
    print("Programa terminado.")
finally:
    ser.close()