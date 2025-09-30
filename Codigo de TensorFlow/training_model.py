#!/usr/bin/env python3
import pandas as pd
import numpy as np
import tensorflow as tf
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import MinMaxScaler, LabelEncoder
import os
import joblib

# ==========================
# Paso 1: Cargar y preprocesar el dataset
# ==========================

def load_data_from_folders(data_path):
    all_data = []
    labels = []
    
    # Recorre cada carpeta dentro de la carpeta principal de gestos
    for sign_folder in os.listdir(data_path):
        sign_path = os.path.join(data_path, sign_folder)
        if os.path.isdir(sign_path):
            # Recorre cada archivo CSV dentro de la carpeta de la seña
            for file_name in os.listdir(sign_path):
                if file_name.endswith('.csv'):
                    file_path = os.path.join(sign_path, file_name)
                    df = pd.read_csv(file_path, header=None)
                    
                    # Convertir los datos a un array plano
                    data_row = df.values.flatten()
                    all_data.append(data_row)
                    
                    # Asignar la etiqueta (nombre de la carpeta)
                    labels.append(sign_folder)

    return np.array(all_data), np.array(labels)

# Definir la ruta a tu carpeta de gestos
DATA_PATH = "./gestos"
if not os.path.exists(DATA_PATH):
    raise FileNotFoundError(f"No se encontró la carpeta de gestos en: {DATA_PATH}")

X, y = load_data_from_folders(DATA_PATH)

# Asegurarse de que las grabaciones tienen la misma longitud (todos los arrays deben tener la misma forma)
if len(np.unique([len(row) for row in X])) > 1:
    print("¡Advertencia! Las grabaciones no tienen la misma longitud. Esto puede causar un error. "
          "Asegúrate de que cada archivo CSV tiene el mismo número de filas y columnas.")
    
# Normalización de los datos (ajustar valores entre 0 y 1)
scaler = MinMaxScaler()
X_scaled = scaler.fit_transform(X)

# Codificación de etiquetas (por ejemplo: 'hola' -> 0, 'adios' -> 1)
label_encoder = LabelEncoder()
y_encoded = label_encoder.fit_transform(y)

print(f"Dataset cargado: {X.shape[0]} muestras, {X.shape[1]} features, {len(np.unique(y_encoded))} clases.")

# ==========================
# Paso 2: Split Train/Test
# ==========================
X_train, X_test, y_train, y_test = train_test_split(
    X_scaled, y_encoded, test_size=0.2, random_state=42, stratify=y_encoded
)

# ==========================
# Paso 3: Construcción del modelo
# ==========================
input_shape = X_train.shape[1]
num_classes = len(np.unique(y_encoded))

model = tf.keras.Sequential([
    tf.keras.layers.Dense(128, activation='relu', input_shape=(input_shape,)),
    tf.keras.layers.Dropout(0.2),
    tf.keras.layers.Dense(64, activation='relu'),
    tf.keras.layers.Dense(num_classes, activation='softmax')
])

model.compile(optimizer='adam',
              loss='sparse_categorical_crossentropy',
              metrics=['accuracy'])

model.summary()
print("\nEntrenando modelo...")

# ==========================
# Paso 4: Entrenamiento
# ==========================
history = model.fit(
    X_train, y_train,
    epochs=7,
    batch_size=32,
    validation_split=0.1,
    verbose=1
)

# ==========================
# Paso 5: Evaluación y guardado
# ==========================
loss, accuracy = model.evaluate(X_test, y_test, verbose=0)
print(f"\nPrecisión en test: {accuracy:.4f}")

# Guardar el modelo para su uso futuro
model.save("signalink_model.h5")
print("Modelo guardado en signalink_model.h5")

# Guardar el codificador de etiquetas para decodificar predicciones
joblib.dump(label_encoder, "label_encoder.pkl")
print("Codificador de etiquetas guardado en label_encoder.pkl")