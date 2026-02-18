# Libre Computer S905X Personal Assistant

A comprehensive personal assistant system designed for the Libre Computer S905X CC V1 single board computer.

## Features

- 🎤 **Voice Recognition & Commands** - Wake word detection and natural language processing
- 🏠 **Smart Home Integration** - Control IoT devices, lights, thermostats
- 🤖 **AI-Powered Responses** - Local and cloud-based AI for intelligent conversations
- 📅 **Calendar & Reminders** - Schedule management and notifications
- 🌦️ **Weather & News** - Real-time updates and briefings
- 🎵 **Music & Media Control** - Spotify, local media, streaming services
- 📱 **Mobile App Integration** - Remote control via smartphone
- 🔒 **Privacy-First** - Local processing with optional cloud features
- 📊 **System Monitoring** - Hardware stats, temperature, performance

## Hardware Requirements

- Libre Computer S905X CC V1 board
- USB microphone or audio HAT
- Speakers or audio output
- MicroSD card (32GB+ recommended)
- Optional: Camera module for visual recognition
- Optional: GPIO sensors (temperature, motion, etc.)

## Software Stack

- **OS**: Ubuntu/Debian for ARM64
- **Voice**: SpeechRecognition, pyttsx3, wake word detection
- **AI**: OpenAI API, local LLM options (Ollama)
- **Smart Home**: Home Assistant integration, MQTT
- **Web Interface**: Flask/FastAPI dashboard
- **Database**: SQLite for local data

## Quick Start

1. Flash OS to microSD card
2. Install dependencies: `./install.sh`
3. Configure audio: `./setup_audio.sh`
4. Start assistant: `python main.py`

## Project Structure

```
├── src/
│   ├── voice/          # Voice recognition and TTS
│   ├── ai/             # AI processing and responses
│   ├── smart_home/     # IoT device integration
│   ├── web/            # Web dashboard
│   └── utils/          # Utilities and helpers
├── config/             # Configuration files
├── scripts/            # Setup and utility scripts
├── requirements.txt    # Python dependencies
└── main.py            # Main application entry point
```

## Development Status

🚧 **Work in Progress** - Setting up core architecture and basic voice recognition.