# LinkChat — Mensajería P2P en Capa de Enlace (Ethernet)

LinkChat es una aplicación de mensajería y transferencia de archivos punto a punto sobre Ethernet (capa 2). No usa IP/TCP/UDP. Construye frames con un EtherType propio y un protocolo fiable (fragmentación, ACKs, CRC32, ventana).

- EtherType: `0x88B5`
- Lenguaje: C++20 (solo STL)
- Plataforma: Linux (interfaces físicas y virtuales; soportado en Docker)
- Permisos: requiere `sudo` o `CAP_NET_RAW` para sockets RAW

---

## Contenido

- Características
- Arquitectura y Protocolo
- Requisitos
- Compilación (nativo)
- Ejecución (nativo)
- Docker (script de orquestación)
- Uso rápido del CLI

---

## Características

- Frames Ethernet crudos (AF_PACKET) con EtherType propio
- Fiabilidad: ACKs acumulativos, retransmisión por timeout, ventana deslizante
- Fragmentación y reensamblado con CRC32
- CLI interactivo: `config`, `chat`, `groupchat`, `send`, `discover`, `info`, `exit`
- Descubrimiento L2 (broadcast HELLO) con alias del usuario
- Transferencia de archivos con preservación de nombre
- Descubrimiento periódico y comandos en chat: `/online`, `/contacts`, `/connect <alias>`

---

## Arquitectura y Protocolo

Módulos principales:
- `src/net/eth_adapter.*`: sockets RAW, RX/TX y construcción de frames
- `src/net/app_eth_bind.*`: enlace de la app con Ethernet (hilo RX)
- `src/app.*`: lógica de envío/recepción, ACKs, ventana y entrega
- `src/reassembly.*`: reensamblado confiable
- `src/sender.*`: planificador de envíos y RTO
- `src/pdu.*`, `src/header.*`, `src/util/crc32.*`: serialización y checksum

PDU: `Header(15B) + payload + CRC32(payload)`. Tipos: `MSG`, `FILE`, `HELLO`, `ACK`.

---

## Requisitos

- Linux con soporte `AF_PACKET`
- CMake ≥ 3.16 y compilador C++20
- Permisos de red cruda (`sudo` o `CAP_NET_RAW`)

---

## Compilación (nativo)

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

Binario resultante: `build/linkchat`

Para evitar `sudo`, concede capacidad:

```bash
sudo setcap cap_net_raw+ep build/linkchat
```

---

## Ejecución (nativo)

1) Lanza el binario: `./linkchat` (o `sudo ./linkchat` si no usaste `setcap`).
2) Configura parámetros básicos:
   - `config` → Interface (ej. `eth0`), MAC destino, MTU, ventana, RTO, carpeta de descargas y alias
3) Descubre pares (opcional, modo consola): `discover` (muestra alias y MAC)
4) Inicia chat: `chat` (dentro de chat puedes usar `/online`, `/contacts`, `/connect <alias>`, `/sendfile <path>`, `/quit`).
   También disponible `groupchat` para un chat grupal (envía a todos los peers online).

Nota: para entrar en `chat` necesitas una MAC destino válida. Puedes obtenerla con `discover` y luego seleccionarla con `/connect <alias>` dentro del chat.

---

## Docker (script de orquestación)

Se incluye un `script.sh` que crea la red, construye la imagen y levanta dos contenedores para pruebas.

Comandos principales:

```bash
# Construir imagen, crear red y levantar dos contenedores (lc1 y lc2)
./script.sh up

# Adjuntarte a lc1 o lc2
./script.sh attach lc1
./script.sh attach lc2

# Detener y eliminar contenedores y red
./script.sh down

# Eliminar también la imagen
./script.sh clean

# Reconstruir rápidamente (down + up)
./script.sh rebuild
```

Detalles:
- Red: `linkchat-net` (bridge)
- Contenedores: `lc1`, `lc2`
- Capacidad: `NET_RAW` (para AF_PACKET)
- Volúmenes: `./inbox1` → `/inbox` (lc1), `./inbox2` → `/inbox` (lc2)

Dentro de cada contenedor:
- Interfaz: `eth0`
- Usa `discover` para ver el alias y MAC del otro contenedor (y luego `config`/`chat`)

---

## Uso rápido del CLI

Comandos globales:
- `help` — lista de comandos
- `config` — define interfaz, MAC destino, MTU, ventana, RTO, carpeta de descargas y alias
- `discover` — broadcast HELLO por 10s (muestra alias y MAC de pares)
- `info` — muestra la configuración actual
- `chat` — inicia chat interactivo; en chat: escribe mensajes y usa subcomandos
- `groupchat` — inicia un chat grupal (envía a todos los peers online)
- `send <path>` — envía un archivo directamente (y regresa al prompt)
- `exit` — salir

En chat interactivo:
- `/online` — lista pares en línea (alias + MAC + last seen)
- `/contacts` — lista todos los contactos vistos (estado online/offline)
- `/connect <alias>` — cambia el destino por el alias indicado
- `/sendfile <path>` — envía archivo al destino activo
- `/quit` — salir del chat

En groupchat:
- Envía mensajes a todos los peers online por defecto (multi-unicast confiable)
- Gestiona destinatarios con `/members list | add <alias> | del <alias|mac> | clear`
- `/online`, `/contacts`, `/sendfile <path>`, `/quit` disponibles

---

## Notas

- En algunos hosts, los bridges pueden filtrar tramas; Docker bridge estándar funciona con `NET_RAW`.
- Si usas host físico, ejecuta con interfaz real (p.ej. `wlo1`, `enpXsY`) y privilegios adecuados.
