#!/usr/bin/env python3
"""
Web Dashboard for Tiny Home Personal Assistant
4K TV optimized interface
"""

from fastapi import FastAPI, Request, WebSocket
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse
import json
import asyncio
from datetime import datetime

app = FastAPI()
templates = Jinja2Templates(directory="templates")
app.mount("/static", StaticFiles(directory="static"), name="static")

# Global state
assistant_state = {
    "devices": {},
    "voice_active": False,
    "last_command": "",
    "weather": {"temp": 72, "condition": "Sunny"},
    "reminders": [],
    "current_app": "Home"
}

@app.get("/", response_class=HTMLResponse)
async def dashboard(request: Request):
    """Main dashboard for 55" 4K TV"""
    return templates.TemplateResponse("dashboard.html", {
        "request": request,
        "state": assistant_state,
        "time": datetime.now().strftime("%I:%M %p"),
        "date": datetime.now().strftime("%A, %B %d")
    })

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket for real-time updates"""
    await websocket.accept()
    try:
        while True:
            # Send state updates
            await websocket.send_json(assistant_state)
            await asyncio.sleep(1)
    except Exception as e:
        print(f"WebSocket error: {e}")

@app.post("/api/tv/power/{action}")
async def tv_power(action: str):
    """TV power control API"""
    if action == "on":
        # Trigger TV power on
        assistant_state["current_app"] = "Home"
        return {"status": "TV turning on"}
    elif action == "off":
        # Trigger TV power off
        assistant_state["current_app"] = "Off"
        return {"status": "TV turning off"}

@app.post("/api/lights/{device_id}/{action}")
async def control_lights(device_id: str, action: str):
    """Light control API"""
    if device_id not in assistant_state["devices"]:
        assistant_state["devices"][device_id] = {"type": "light", "state": "off"}
    
    assistant_state["devices"][device_id]["state"] = action
    return {"status": f"Light {device_id} {action}"}

@app.post("/api/voice/command")
async def voice_command(command: dict):
    """Process voice command"""
    assistant_state["last_command"] = command.get("text", "")
    assistant_state["voice_active"] = True
    
    # Process command here
    response = f"Processed: {command.get('text', '')}"
    
    # Reset voice active after delay
    await asyncio.sleep(3)
    assistant_state["voice_active"] = False
    
    return {"response": response}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)