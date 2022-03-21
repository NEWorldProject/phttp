# phttp
Poor man's http protocol for transporting a status/run line, a header block and a content block

## 0. Motivation
The http protocol is good, but very difficult to parse and is too complicated to implement. 
During the development of one of my projects, I needed a very lightweight solution for
transmitting messages between internal services, so this protocol is developed.

## 1. Concepts and Structure
### 1.1 Connection
The protocol is built on a TCP connection and is intended for LAN usage.
Messages are transmitted as a stream of blocks without deduplication.
TLS could be implemented over raw TCP streams for added security.

### 1.2 Messages
#### 1.2.1 RequestLine Message
The request message consists of a run line block, a headers block, and data blocks.
#### 1.2.2 ResponseLine Message
The run message consists of a response line block, a headers block, and data blocks.
#### 1.2.3 Blocks
Each block contains its message id for potential interleaved blocks. 
Block is formatted as following:
```
int32_le message_id;
int32_le block_size;
byte[block_size] content;
```
Data-blocks are sequences in the same order as they appeared in the stream.
#### 1.2.4 phttp_string
```
int32_le utf8_length;
char8_t[utf8_length] content;
```
#### 1.2.5 RequestLine Line
```
phttp_string verb;
phttp_string version;
phttp_string resource_location;
```
#### 1.2.6 ResponseLine Line
```
int32_le http_status_code;
phttp_string http_response_message;
```
#### 1.2.7 Headers
```
int32_le entry_count;
phttp_string[entry_count][2] header_entries;
```