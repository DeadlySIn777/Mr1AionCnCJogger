#!/usr/bin/env python3
"""
Libre Computer S905X Personal Assistant
Main application entry point
"""

import os
import sys
import asyncio
import logging
from datetime import datetime

# Add src directory to Python path
sys.path.append(os.path.join(os.path.dirname(__file__), 'src'))

from voice.speech_recognition import VoiceRecognizer
from voice.text_to_speech import TextToSpeech
from ai.assistant_brain import AssistantBrain
from smart_home.device_manager import DeviceManager
from utils.config import Config
from utils.logger import setup_logger

class PersonalAssistant:
    """Main Personal Assistant class"""
    
    def __init__(self):
        self.config = Config()
        self.logger = setup_logger()
        self.voice_recognizer = VoiceRecognizer()
        self.tts = TextToSpeech()
        self.brain = AssistantBrain()
        self.device_manager = DeviceManager()
        self.running = False
        
    async def start(self):
        """Start the personal assistant"""
        self.logger.info("Starting Libre Computer Personal Assistant...")
        
        # Initialize components
        await self.voice_recognizer.initialize()
        await self.tts.initialize()
        await self.brain.initialize()
        await self.device_manager.initialize()
        
        # Welcome message
        await self.tts.speak("Hello! I'm your personal assistant. How can I help you today?")
        
        self.running = True
        await self.main_loop()
    
    async def main_loop(self):
        """Main processing loop"""
        while self.running:
            try:
                # Listen for voice commands
                command = await self.voice_recognizer.listen()
                
                if command:
                    self.logger.info(f"Received command: {command}")
                    
                    # Process command through AI brain
                    response = await self.brain.process_command(command)
                    
                    # Execute any device actions
                    if response.get('device_actions'):
                        await self.device_manager.execute_actions(response['device_actions'])
                    
                    # Speak response
                    if response.get('text'):
                        await self.tts.speak(response['text'])
                
                await asyncio.sleep(0.1)
                
            except KeyboardInterrupt:
                self.logger.info("Shutting down...")
                self.running = False
            except Exception as e:
                self.logger.error(f"Error in main loop: {e}")
                await asyncio.sleep(1)
    
    async def stop(self):
        """Stop the personal assistant"""
        self.running = False
        await self.tts.speak("Goodbye!")
        self.logger.info("Personal Assistant stopped")

async def main():
    """Main function"""
    assistant = PersonalAssistant()
    
    try:
        await assistant.start()
    except KeyboardInterrupt:
        await assistant.stop()
    except Exception as e:
        logging.error(f"Fatal error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    # Check if running on supported platform
    if not os.path.exists('/proc/cpuinfo'):
        print("Warning: This assistant is designed for ARM64 Linux systems")
    
    print(f"Libre Computer S905X Personal Assistant v1.0")
    print(f"Starting at {datetime.now()}")
    
    asyncio.run(main())