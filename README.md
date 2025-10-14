# LinkChat — Mensajería P2P en Capa de Enlace (Ethernet)

**LinkChat** es una aplicación de mensajería y transferencia de archivos **punto a punto** que opera **exclusivamente en la capa de enlace** (Ethernet). No usa IP/TCP/UDP; construye frames Ethernet con un **EtherType propio** y un protocolo minimalista con **fragmentación, CRC32, ACKs y control de flujo**.

- **EtherType:** `0x88B5`  
- **Lenguaje:** C++20 (solo biblioteca estándar)  
- **Plataformas:** Linux (soporta interfaces físicas y virtuales / Docker)  
- **Permisos:** requiere `sudo` o `CAP_NET_RAW` para sockets RAW

---

## Tabla de contenidos

- [LinkChat — Mensajería P2P en Capa de Enlace (Ethernet)](#linkchat--mensajería-p2p-en-capa-de-enlace-ethernet)
  - [Tabla de contenidos](#tabla-de-contenidos)
  - [Características](#características)
  - [Arquitectura \& Protocolo](#arquitectura--protocolo)
  - [Requisitos](#requisitos)
  - [Compilación](#compilación)

---

## Características

- ✅ Frames Ethernet crudos (AF_PACKET) con EtherType propio (`0x88B5`)
- ✅ **Fiabilidad:** ACK acumulativos, retransmisión por timeout, ventana deslizante
- ✅ **Fragmentación & reensamblado** con CRC32 (IEEE reflejado, `0xEDB88320`)
- ✅ **CLI interactivo**: `config`, `chat`, `send`, `discover`, `info`, `exit`
- ✅ **Descubrimiento L2 (broadcast)**: anuncia *nick* y muestra pares (sin IP)
- ✅ **Transferencia de archivos** (con preservación de nombre y carpeta destino)
- ✅ **Docker bridge**: demo de LAN virtual capa 2 (dos contenedores)
- 🟨 Ventana > 1 y RTO adaptativo (base lista; optimizaciones en roadmap)
- 🟨 Broadcast “uno-a-todos” (modo best-effort) — extra opcional

---

## Arquitectura & Protocolo

**Módulos principales:**
- `eth_adapter`: sockets RAW (RX/TX), construcción y filtrado de frames  
- `app_eth_bind`: hilo RX que enlaza Ethernet ↔ LinkchatApp  
- `LinkchatApp`: coordina envío/recepción, ACKs, ventana, reensamblado  
- `reassembly`: almacena chunks, detecta duplicados, arma mensaje completo  
- `sender`: ventana deslizante, RTO, on_tick/on_ack  
- `pdu/header/crc`: serialización de header, payload+CRC, parseos  

**Header (15B, big-endian):** `type, msg_id, seq, total, payload_len`  
**PDU:** `Header + payload + CRC32(payload-only)`  
**ACK:** acumulativo `AckFields { msg_id, highest_seq_ok }`

**Tipos relevantes (`Type`):** `MSG`, `FILE`, `HELLO`, `ACK` (interno)

---

## Requisitos

- Linux (kernel con `AF_PACKET`)
- CMake ≥ 3.16 y compilador C++20
- Permisos de red cruda (`sudo` o `CAP_NET_RAW`)

---

## Compilación

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
