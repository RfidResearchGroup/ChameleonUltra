#!/bin/bash
# Quick Setup Script for ChameleonUltra LF Debug (Root Directory)
# ===============================================================

echo "🔧 Installing Python dependencies..."

# Check if pip3 exists, otherwise use pip
if command -v pip3 &> /dev/null; then
    PIP_CMD="pip3"
elif command -v pip &> /dev/null; then
    PIP_CMD="pip"
else
    echo "❌ Neither pip nor pip3 found. Please install Python pip first."
    exit 1
fi

echo "📦 Installing pyserial..."
$PIP_CMD install pyserial

echo "✅ Dependencies installed!"
echo ""
echo "🚀 Now you can run:"
echo "   python3 chameleon_lf_debug_root.py --port /dev/tty.usbmodem*"
echo ""
echo "🔧 Or test specific components:"
echo "   python3 chameleon_lf_debug_root.py --test status_codes"
echo "   python3 chameleon_lf_debug_root.py --test connection"
echo "   python3 chameleon_lf_debug_root.py --test lf_commands"

