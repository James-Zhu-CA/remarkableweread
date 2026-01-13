#!/bin/bash
# Quick Deploy Script for WeRead E-Ink Browser
# Usage: ./quick-deploy.sh [DEVICE_IP] [DEVICE_PASSWORD]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
DEVICE_IP="${1:-10.11.99.1}"
DEVICE_PASS="${2:-}"

echo -e "${GREEN}=== WeRead E-Ink Browser - Quick Deploy ===${NC}"
echo ""

# Check if password is provided
if [ -z "$DEVICE_PASS" ]; then
    echo -e "${YELLOW}Device password not provided.${NC}"
    echo "Usage: $0 <DEVICE_IP> <DEVICE_PASSWORD>"
    echo "Example: $0 10.11.99.1 MyPassword"
    exit 1
fi

# Check if sshpass is installed
if ! command -v sshpass &> /dev/null; then
    echo -e "${RED}Error: sshpass is not installed${NC}"
    echo "Install it with:"
    echo "  macOS: brew install hudochenkov/sshpass/sshpass"
    echo "  Linux: sudo apt-get install sshpass"
    exit 1
fi

# Check if binary exists
BINARY_PATH="src/build-cross/WereadEinkBrowser"
if [ ! -f "$BINARY_PATH" ]; then
    echo -e "${RED}Error: Binary not found at $BINARY_PATH${NC}"
    echo "Please compile the project first:"
    echo "  docker exec qt6-arm-builder bash -c 'cd /workspace/src && cmake --build build-cross'"
    exit 1
fi

echo -e "${GREEN}✓${NC} Binary found: $BINARY_PATH"

# Test connection
echo -e "\n${YELLOW}Testing connection to $DEVICE_IP...${NC}"
if ! sshpass -p "$DEVICE_PASS" ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no root@$DEVICE_IP "echo 'Connection OK'" &> /dev/null; then
    echo -e "${RED}Error: Cannot connect to device${NC}"
    echo "Please check:"
    echo "  1. Device IP is correct: $DEVICE_IP"
    echo "  2. Device is on the same network"
    echo "  3. SSH is enabled on device"
    echo "  4. Password is correct"
    exit 1
fi
echo -e "${GREEN}✓${NC} Connection successful"

# Check disk space
echo -e "\n${YELLOW}Checking disk space...${NC}"
DISK_SPACE=$(sshpass -p "$DEVICE_PASS" ssh root@$DEVICE_IP "df -h /home | tail -1 | awk '{print \$4}'")
echo -e "${GREEN}✓${NC} Available space: $DISK_SPACE"

# Upload binary to temp location
echo -e "\n${YELLOW}Uploading binary to device...${NC}"
if ! sshpass -p "$DEVICE_PASS" scp -o StrictHostKeyChecking=no "$BINARY_PATH" root@$DEVICE_IP:/tmp/WereadEinkBrowser.new; then
    echo -e "${RED}Error: Failed to upload binary${NC}"
    exit 1
fi
echo -e "${GREEN}✓${NC} Binary uploaded to /tmp/WereadEinkBrowser.new"

# Create application directory
echo -e "\n${YELLOW}Creating application directory...${NC}"
sshpass -p "$DEVICE_PASS" ssh root@$DEVICE_IP "mkdir -p /home/root/weread"
echo -e "${GREEN}✓${NC} Directory created: /home/root/weread"

# Copy binary to final location
echo -e "\n${YELLOW}Installing binary...${NC}"
sshpass -p "$DEVICE_PASS" ssh root@$DEVICE_IP "cp /tmp/WereadEinkBrowser.new /home/root/weread/WereadEinkBrowser && chmod +x /home/root/weread/WereadEinkBrowser"
echo -e "${GREEN}✓${NC} Binary installed"

# Verify installation
echo -e "\n${YELLOW}Verifying installation...${NC}"
TIMESTAMP=$(sshpass -p "$DEVICE_PASS" ssh root@$DEVICE_IP "stat -c '%Y' /home/root/weread/WereadEinkBrowser")
echo -e "${GREEN}✓${NC} Installation verified (timestamp: $TIMESTAMP)"

# Clean up temp file
sshpass -p "$DEVICE_PASS" ssh root@$DEVICE_IP "rm -f /tmp/WereadEinkBrowser.new"

echo ""
echo -e "${GREEN}=== Deployment Complete! ===${NC}"
echo ""
echo "To start the application:"
echo "  1. SSH to device: ssh root@$DEVICE_IP"
echo "  2. Stop system UI: systemctl stop xochitl"
echo "  3. Run application: /home/root/weread/WereadEinkBrowser"
echo ""
echo "To restore system UI:"
echo "  systemctl start xochitl"
echo ""
