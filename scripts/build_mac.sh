#!/bin/bash
# Build script for macOS HDF5 Export GUI Application
# Creates a standalone .app bundle and optionally a .dmg using PyInstaller

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo -e "${GREEN}Building HDF5 Export GUI Application for macOS...${NC}"

# Parse arguments
CLEAN=false
CREATE_DMG=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=true
            shift
            ;;
        --dmg)
            CREATE_DMG=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--clean] [--dmg]"
            exit 1
            ;;
    esac
done

# Clean previous builds if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}Cleaning previous builds...${NC}"
    rm -rf build dist __pycache__ *.spec.dist
    find . -type d -name "*.spec.dist" -exec rm -rf {} + 2>/dev/null || true
fi

# Check for Python
echo -e "${CYAN}Checking Python installation...${NC}"
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}ERROR: Python 3 not found. Please install Python 3.8 or later.${NC}"
    exit 1
fi

PYTHON_VERSION=$(python3 --version)
echo -e "${GREEN}Found: $PYTHON_VERSION${NC}"

# Create virtual environment if it doesn't exist
VENV_PATH=".venv"
if [ ! -d "$VENV_PATH" ]; then
    echo -e "${CYAN}Creating virtual environment...${NC}"
    python3 -m venv "$VENV_PATH"
fi

# Activate virtual environment
echo -e "${CYAN}Activating virtual environment...${NC}"
source "$VENV_PATH/bin/activate"

# Upgrade pip
echo -e "${CYAN}Upgrading pip...${NC}"
pip install --upgrade pip

# Install dependencies
echo -e "${CYAN}Installing dependencies...${NC}"
pip install -r requirements.txt

# Build with PyInstaller
echo -e "${CYAN}Building application bundle with PyInstaller...${NC}"
pyinstaller hdf5_export.spec --clean

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Build successful!${NC}"
    
    # Check if .app exists
    APP_PATH="dist/hdf5_export_app.app"
    if [ -d "$APP_PATH" ]; then
        echo -e "${GREEN}Application bundle: $APP_PATH${NC}"
        
        # Get app size
        APP_SIZE=$(du -sh "$APP_PATH" | cut -f1)
        echo -e "${CYAN}Bundle size: $APP_SIZE${NC}"
        
        # Create .dmg if requested
        if [ "$CREATE_DMG" = true ]; then
            echo -e "${CYAN}Creating DMG...${NC}"
            
            DMG_NAME="hdf5_export_app"
            DMG_PATH="dist/${DMG_NAME}.dmg"
            
            # Remove existing DMG if present
            [ -f "$DMG_PATH" ] && rm -f "$DMG_PATH"
            
            # Create DMG using hdiutil
            hdiutil create -volname "HDF5 Export App" -srcfolder "$APP_PATH" \
                -ov -format UDZO "$DMG_PATH"
            
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}DMG created: $DMG_PATH${NC}"
            else
                echo -e "${YELLOW}Warning: DMG creation failed. You can create it manually using:${NC}"
                echo -e "${CYAN}hdiutil create -volname \"HDF5 Export App\" -srcfolder \"$APP_PATH\" -ov -format UDZO \"$DMG_PATH\"${NC}"
            fi
        else
            echo -e "${CYAN}To create a DMG, run: $0 --dmg${NC}"
        fi
    else
        echo -e "${RED}ERROR: Application bundle not found at $APP_PATH${NC}"
        exit 1
    fi
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Done!${NC}"
