1) Data Link Layer (Layer 2) y Ethernet

Rol: comunica hosts dentro de la misma LAN mediante Ethernet frames (no usa IP/TCP/UDP).

Ethernet II frame (estructura):
Destination MAC (6B) | Source MAC (6B) | EtherType (2B) | Payload (46–1500B) | FCS/CRC32 (4B)

MAC address (48 bits): identificador único por interfaz (origen/destino del frame).

Unicast / Broadcast / Multicast: 1-a-1, 1-a-todos (FF:FF:FF:FF:FF:FF), 1-a-grupo.

MTU: 1500 bytes de Payload; si tu data real <46B, la NIC añade padding (ceros).

FCS (CRC32): la NIC calcula y verifica integridad del frame (detección de errores).

2) RAW Sockets en Linux

Qué son: acceso directo a raw frames a nivel Layer 2 (dominio AF_PACKET, tipo SOCK_RAW).

Apertura típica: socket(AF_PACKET, SOCK_RAW, htons(<your EtherType>)).

Bind a interfaz: bind() con sockaddr_ll y if_nametoindex("eth0"/"wlo1").

Permisos: necesitas CAP_NET_RAW (root o capability en Docker).

Filtro por EtherType: abrir con tu custom EtherType evita tener que filtrar todo el tráfico (ETH_P_ALL).

3) Protocolo propio sobre Ethernet (dentro del Payload)

Custom EtherType: valor de 2 bytes para identificar tu protocolo (p.ej. 0x88B5).

Minimal header (MVP):

TYPE (1B): MSG, FILE_CHUNK, ACK, HELLO, HELLO_ACK.

SEQ (4B): secuencia para fiabilidad/reensamblaje.

TOTAL (4B): tamaño total (del mensaje o del archivo).

PAYLEN (2B): longitud real de data útil en este frame (para ignorar padding).
Todo en network byte order (big-endian).

Payload: bytes de tu mensaje/archivo; PAYLEN indica cuántos copiar/interpretar.

4) Fiabilidad (reliable delivery) sin TCP: ARQ

Stop-and-Wait ARQ (mensajes): emites con SEQ, esperas ACK con el mismo SEQ.

Timeout + retransmisión (backoff) si no llega ACK.

Archivos (FILE_CHUNK): fragmentas por MTU; puedes usar ventana pequeña con ACK por chunk o ACK coalescido.

Control de duplicados: mantén una ventana de SEQ recientes para descartar repeticiones (especialmente en broadcast).

5) Fragmentation & Reassembly (archivos)

Fragmentation: partes el archivo en chunks que quepan en MTU_effective (p. ej. ~1400B de data útil por frame).

Reassembly: el receptor guarda por SEQ (o CHUNK_INDEX si lo añades) hasta reconstruir TOTAL.

Integridad adicional: opcional añadir CRC/hash por chunk en un campo u opción; la NIC ya cubre el FCS del frame, pero esto protege el contenido del archivo end-to-end.

6) Discovery (HELLO) y Broadcast

HELLO (broadcast): envío a FF:FF:FF:FF:FF:FF para anunciar presencia.

HELLO_ACK (unicast o broadcast controlado): el par responde; construyes peers table (MAC, alias, last-seen).

Nota Wi-Fi: algunos APs filtran/limitan broadcast; ten plan alterno (cable/Docker) para demo.

7) Interfaz de Usuario (CLI) y flujo básico

CLI mínima (MVP):

recv (escucha)

send <MAC> <text> (MSG + ACK)

sendfile <MAC> <path> (FILE_CHUNK + ACKs)

scan (HELLO/HELLO_ACK)

broadcast <msg> (uno-a-todos, con control de duplicados)

Flujo típico: scan → seleccionar peer → send o sendfile → esperar ACK → mostrar progreso/resultado.

8) Entorno de pruebas: físico + Docker

LAN física: ejecuta con privilegios; valida interfaz (eth0/wlo1) y MAC local.

Docker (bridge network): añade CAP_NET_RAW al contenedor; simula múltiples hosts y prueba sin interferencias del Wi-Fi.

Wireshark/tcpdump: filtra por ether proto 0x<tu EtherType> para inspeccionar frames.

9) Seguridad (opcional / extra)

Cifrado simétrico del Payload (p.ej. AES-GCM con clave precompartida en config) para confidencialidad e integridad de datos de aplicación.

Impacto en header: una flag en TYPE/FLAGS para indicar “encrypted”, y el resto igual (ACK/SEQ/TOTAL/PAYLEN siguen funcionando).

10) Consideraciones de diseño y buenas prácticas

Endianness: usa siempre htons/htonl al escribir y ntohs/ntohl al leer.

Bounds checking: verifica que header + PAYLEN ≤ received_len.

Timers: ajusta timeouts según RTT medido en tu LAN; empieza conservador, afina tras pruebas.

Errores claros: distingue “timeout sin ACK” vs “frame corrupto” vs “peer no encontrado”.

Logs y métricas: cuenta TX/RX, retransmisiones, throughput, y ratio de pérdidas.

11) Limitaciones y trade-offs del enfoque Layer 2

Alcance: solo misma LAN (no enruta entre subredes).

Fiabilidad artesanal: tú implementas ACK/RTT/retry (flexible, pero más trabajo que usar TCP).

Broadcast en Wi-Fi: puede degradar en APs saturados o con aislamiento de clientes.

Portabilidad: depende de AF_PACKET/Linux y CAP_NET_RAW.

12) Glosario rápido (English → español)

Frame: unidad de transmisión de Ethernet (trama).

MAC address: identificador único de interfaz (48 bits).

EtherType: campo que identifica el protocolo encapsulado en Payload.

Payload: datos de tu aplicación dentro del frame.

FCS/CRC32: verificación de integridad del frame a nivel físico/enlace.

RAW socket: socket que expone frames sin procesar (Layer 2).

MTU: máximo tamaño de Payload sin fragmentación.

ACK/SEQ: acuse de recibo / número de secuencia para fiabilidad.

Broadcast: envío a todos los hosts de la LAN.

Discovery (HELLO): mecanismo para detectar peers.



Resumen Día 1 — Linkchat (capa de enlace)
Qué logramos

Definimos el formato de trama (PDU):
[Header 15B (big-endian)] [Payload P] [CRC32 del payload 4B (big-endian)].

Header minimalista (15 bytes):
type (u8) · msg_id (u32) · seq (u32) · total (u32) · payload_len (u16).
type es enum class Type { MSG=1, FILE=2, ACK=3, HELLO=4 }.

Integridad: CRC32 IEEE (polinomio reflejado 0xEDB88320, init/xorout 0xFFFFFFFF) calculado solo sobre el payload.

Implementado hoy

CRC32 (bitwise, reflejado) con self-tests canónicos.

Header:

serialize_header (15B, big-endian, validaciones básicas).

parse_header (lectura BE→host y validación de type).

Helpers BE comunes (store/load u16/u32) y constantes (kHeaderSize=15, kCrcSize=4).

PDU:

build_pdu: escribe header, copia payload, calcula y escribe CRC (u32 BE).

parse_pdu: parsea header, verifica payload_len vs tamaño real, valida CRC y extrae payload.