#!/usr/bin/env python3
"""
Launcher for Edge Condition Monitoring Dashboard
Runs FastAPI backend and provides quick instructions for Vite frontend.
"""
import os
import sys
import subprocess

def main():
    backend_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "backend"))
    frontend_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "frontend"))

    print("=" * 70)
    print("  TINYML EDGE VIBRATION ANOMALY MONITORING DASHBOARD")
    print("=" * 70)
    print(f"Backend Directory : {backend_dir}")
    print(f"Frontend Directory: {frontend_dir}")
    print("\nStarting FastAPI Backend on http://localhost:8000 ...")
    print("Interactive API Docs: http://localhost:8000/docs")
    print("WebSocket Endpoint  : ws://localhost:8000/ws")
    print("-" * 70)
    print("To run the frontend simultaneously in a separate terminal:")
    print("  cd dashboard/frontend")
    print("  npm run dev")
    print("  -> Opens http://localhost:5173")
    print("=" * 70)

    os.chdir(backend_dir)
    sys.path.insert(0, backend_dir)
    try:
        import uvicorn
        uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)
    except KeyboardInterrupt:
        print("\nShutting down dashboard backend.")

if __name__ == "__main__":
    main()
