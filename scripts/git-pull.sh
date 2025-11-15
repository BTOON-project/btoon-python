#!/bin/bash
# Pull latest changes for btoon-python and update submodules

set -e

# Color codes
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

cd "$REPO_DIR"

echo -e "${BLUE}📥 Pulling latest changes for btoon-python...${NC}"
echo ""

# Check for uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo -e "${RED}⚠️  You have uncommitted changes:${NC}"
    git status --short
    echo ""
    echo "Please commit or stash your changes before pulling."
    exit 1
fi

# Check submodule for uncommitted changes
if [ -d "core/.git" ]; then
    cd core
    if ! git diff-index --quiet HEAD --; then
        echo -e "${YELLOW}⚠️  Core submodule has uncommitted changes:${NC}"
        git status --short
        echo ""
        read -p "Stash these changes? (y/N) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            git stash save "Auto-stash during pull on $(date)"
            echo -e "${GREEN}✓ Changes stashed${NC}"
        else
            echo -e "${RED}Please handle uncommitted changes manually${NC}"
            exit 1
        fi
    fi
    cd "$REPO_DIR"
fi

# Pull main repository
echo "Pulling btoon-python repository..."
git pull

echo -e "${GREEN}✓ Repository updated${NC}"
echo ""

# Update submodules
echo "Updating submodules..."
git submodule update --init --recursive

echo -e "${GREEN}✓ Submodules updated${NC}"
echo ""

# Show submodule status
echo "Submodule status:"
git submodule status

echo ""
echo -e "${GREEN}✓ All updates complete!${NC}"
