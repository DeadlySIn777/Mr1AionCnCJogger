#!/usr/bin/env python3
"""
LoRa Gateway for ESP32 Smart Home Devices
Communicates with universal ESP32 firmware devices
"""

import serial
import json
import asyncio
import logging
from datetime import datetime

class LoRaGateway:
    def __init__(self, port='/dev/ttyUSB0', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.devices = {}
        self.logger = logging.getLogger(__name__)
        
        # Device types and their capabilities
        self.device_types = {
            'LIGHT_SWITCH': ['on', 'off'],
            'DIMMER_LIGHT': ['on', 'off', 'dim'],
            'RGB_STRIP': ['on', 'off', 'color', 'brightness', 'effect'],
            'FAN_CONTROLLER': ['on', 'off', 'speed', 'direction'],
            'OUTLET_SWITCH': ['on', 'off'],
            'SENSOR_NODE': ['read']
        }
    
    async def connect(self):
        """Connect to LoRa gateway"""
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            self.logger.info(f"Connected to LoRa gateway on {self.port}")
            return True
        except Exception as e:
            self.logger.error(f"Failed to connect to LoRa gateway: {e}")
            return False
    
    async def discover_devices(self):
        """Discover all LoRa devices on network"""
        if not self.serial_conn:
            return False
        
        # Send discovery command
        discovery_cmd = "DISCOVER_ALL\n"
        self.serial_conn.write(discovery_cmd.encode())
        
        # Wait for responses
        await asyncio.sleep(2)
        
        while self.serial_conn.in_waiting:
            response = self.serial_conn.readline().decode().strip()
            if response.startswith("DEVICE:"):
                self.parse_device_info(response)
        
        self.logger.info(f"Discovered {len(self.devices)} devices")
        return True
    
    def parse_device_info(self, response):
        """Parse device discovery response"""
        # Format: DEVICE:ID:001,TYPE:LIGHT_SWITCH,NAME:Living Room,BATTERY:85
        parts = response.split(',')
        device_info = {}
        
        for part in parts:
            if ':' in part:
                key, value = part.split(':', 1)
                device_info[key] = value
        
        if 'ID' in device_info:
            self.devices[device_info['ID']] = device_info
            self.logger.info(f"Added device: {device_info}")
    
    async def send_command(self, device_id, command, value=None):
        """Send command to specific device"""
        if not self.serial_conn:
            self.logger.error("Not connected to LoRa gateway")
            return False
        
        if device_id not in self.devices:
            self.logger.error(f"Device {device_id} not found")
            return False
        
        # Format command
        if value is not None:
            cmd = f"CMD:{device_id}:{command}:{value}\n"
        else:
            cmd = f"CMD:{device_id}:{command}\n"
        
        try:
            self.serial_conn.write(cmd.encode())
            self.logger.info(f"Sent command: {cmd.strip()}")
            
            # Wait for acknowledgment
            await asyncio.sleep(0.5)
            if self.serial_conn.in_waiting:
                response = self.serial_conn.readline().decode().strip()
                self.logger.info(f"Device response: {response}")
                return "ACK" in response
            
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to send command: {e}")
            return False
    
    async def lights_on(self, device_id):
        """Turn on lights"""
        return await self.send_command(device_id, "ON")
    
    async def lights_off(self, device_id):
        """Turn off lights"""
        return await self.send_command(device_id, "OFF")
    
    async def set_brightness(self, device_id, brightness):
        """Set light brightness (0-100)"""
        return await self.send_command(device_id, "BRIGHTNESS", brightness)
    
    async def set_rgb_color(self, device_id, r, g, b):
        """Set RGB color"""
        color_value = f"{r},{g},{b}"
        return await self.send_command(device_id, "COLOR", color_value)
    
    async def fan_speed(self, device_id, speed):
        """Set fan speed (0-100)"""
        return await self.send_command(device_id, "SPEED", speed)
    
    def get_device_list(self):
        """Get list of all devices"""
        return self.devices
    
    def get_device_by_name(self, name):
        """Find device by name"""
        for device_id, info in self.devices.items():
            if info.get('NAME', '').lower() == name.lower():
                return device_id, info
        return None, None
    
    async def close(self):
        """Close connection"""
        if self.serial_conn:
            self.serial_conn.close()
            self.logger.info("LoRa gateway connection closed")

# Test function
async def test_lora_gateway():
    gateway = LoRaGateway()
    
    if await gateway.connect():
        await gateway.discover_devices()
        
        # Test commands
        living_room_id, _ = gateway.get_device_by_name("Living Room")
        if living_room_id:
            await gateway.lights_on(living_room_id)
            await asyncio.sleep(2)
            await gateway.lights_off(living_room_id)
        
        await gateway.close()

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(test_lora_gateway())