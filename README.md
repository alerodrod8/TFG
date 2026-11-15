# NFC Check-in System

This repository contains the hardware, firmware, software, and supporting resources developed for a final degree project (TFG). The project implements an NFC-based check-in system with an STM32 device, ESP32 connectivity, a Flask web server, and a database for check-in records. It also includes NFC card utilities, enclosure models, hardware documentation, and example outputs.

## Repository contents

| Path | Contents |
| --- | --- |
| [3D Models/v2.4](3D%20Models/v2.4/) | 3MF models for the enclosure, supports, LED boards, and logo badge. |
| [STM32 Firmware](STM32%20Firmware/) | STM32 firmware source and headers for hardware revisions [v2.0](STM32%20Firmware/v2.0/) and [v2.4](STM32%20Firmware/v2.4/). |
| [ESP32 Firmware](ESP32%20Firmware/) | ESP32 PlatformIO firmware and an Arduino Uno testing variant. |
| [NFC Code](NFC%20Code/) | PlatformIO projects for reading and writing encrypted NFC data. |
| [Flask Web Server](Flask%20Web%20Server/) | Python Flask server for check-in data and report generation. |
| [Database Code](Database%20Code/) | SQL scripts for creating and populating the database and running queries. |
| [Docs](Docs/) | The [project report](Docs/TFG.pdf), [presentation](Docs/Presentaci%C3%B3n_TFG.pdf), and [hardware revision documents](Docs/Hardware%20Revisions/). |
| [Images](Images/) | [Hardware revision images](Images/Hardware%20Revisions/), [power trees](Images/PowerTrees/), [screen designs](Images/Screens/), and [icons](Images/Icons/). |
| [Outputs](Outputs/) | Example test outputs, including a check-in report, log file, database screenshots, and an email screenshot. |

The report in [Docs/TFG.pdf](Docs/TFG.pdf) provides further details about the design and development of the system.