#!/bin/bash
# ChameleonUltra LF Debug Suite Installation Script
# =================================================

set -e

echo "🚀 ChameleonUltra LF Debug Suite Installer"
echo "=========================================="

# Check if we're in the right directory
if [ ! -d "software/script" ]; then
    echo "❌ Error: This script must be run from the ChameleonUltra root directory"
    echo "   Make sure you're in the directory containing 'software/script/'"
    exit 1
fi

echo "✅ Found ChameleonUltra directory structure"

# Create backup directory
BACKUP_DIR="lf_debug_backups_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"
echo "📁 Created backup directory: $BACKUP_DIR"

# Backup original files
echo "💾 Backing up original files..."
cp software/script/chameleon_enum.py "$BACKUP_DIR/"
cp software/script/chameleon_cmd.py "$BACKUP_DIR/"
cp software/script/chameleon_cli_unit.py "$BACKUP_DIR/"

# Apply fixes
echo "🔧 Applying LF CLI fixes..."
python3 fix_lf_cli.py software/script/

# Copy debug tool
echo "📋 Installing debug tool..."
cp chameleon_lf_debug.py software/script/

# Make scripts executable
chmod +x software/script/chameleon_lf_debug.py
chmod +x fix_lf_cli.py

echo ""
echo "✅ Installation complete!"
echo ""
echo "🧪 To run the debug suite:"
echo "   cd software/script"
echo "   python3 chameleon_lf_debug.py"
echo ""
echo "🔧 Available test options:"
echo "   python3 chameleon_lf_debug.py --test connection"
echo "   python3 chameleon_lf_debug.py --test status_codes"
echo "   python3 chameleon_lf_debug.py --test lf_commands"
echo "   python3 chameleon_lf_debug.py --test all"
echo ""
echo "💾 Original files backed up to: $BACKUP_DIR"
echo ""
echo "🚀 Ready to debug your ChameleonUltra LF functionality!"

