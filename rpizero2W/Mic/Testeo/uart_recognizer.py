#!/usr/bin/python3
import serial
import numpy as np

# Inicialización
ser = serial.Serial('/dev/serial0', 115200)
sample_rate = 16000
buffer_size = 1000 # 1000 Muestras

ADC_MID_POINT = 2048

buffer = []
print(f"Esperando {buffer_size} muestras...")

try:
    while len(buffer) < buffer_size:
        if ser.in_waiting >= 2:
            data = ser.read(2)
            if len(data) == 2:
                val = int.from_bytes(data, byteorder='little', signed=False)
                buffer.append(val)
    print("Captura finalizada")

    # Procesamiento (centrado, pero sin escalar para mantener los valores originales del ADC)
    audio_raw = np.array(buffer, dtype=np.uint16)
    audio_centered = audio_raw.astype(np.int32) - ADC_MID_POINT


    # --- Guardar en archivo ---
    file_name = "mis_muestras.txt"
    with open(file_name, "w") as f:
        for muestra in audio_centered:
            f.write(str(muestra) + "\n")

    print(f"¡Éxito! {len(audio_centered)} muestras guardadas en el archivo '{file_name}'")
            
except KeyboardInterrupt:
    print("\nLectura terminada.")
finally:
    if ser.is_open:
        ser.close()
        print("Puerto serial cerrado.")
