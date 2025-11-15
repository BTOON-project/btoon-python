#!/bin/bash
# Update btoon-python repository with latest core submodule changes

set -e  # Exit on error

echo "🔄 Updating btoon-python with latest core submodule..."
echo ""

# Color codes
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

cd "$REPO_DIR"

# Check if we're in a git repository
if [ ! -d ".git" ]; then
    echo -e "${RED}Error: Not in a git repository${NC}"
    exit 1
fi

# Check for uncommitted changes in main repo
if ! git diff-index --quiet HEAD --; then
    echo -e "${YELLOW}⚠️  You have uncommitted changes in btoon-python${NC}"
    echo "Please commit or stash them before updating the submodule."
    git status --short
    exit 1
fi

echo "📦 Updating core submodule..."
cd core

# Check if core submodule has uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo -e "${YELLOW}⚠️  Core submodule has uncommitted changes:${NC}"
    git status --short
    echo ""
    read -p "Do you want to stash these changes? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        git stash save "Auto-stash before update on $(date)"
        echo -e "${GREEN}✓ Changes stashed${NC}"
    else
        echo -e "${RED}Aborting. Please handle uncommitted changes manually.${NC}"
        exit 1
    fi
fi

# Fetch latest from remote
echo "Fetching latest changes from origin..."
git fetch origin

# Get current and remote commits
CURRENT_COMMIT=$(git rev-parse HEAD)
REMOTE_COMMIT=$(git rev-parse origin/main)

if [ "$CURRENT_COMMIT" = "$REMOTE_COMMIT" ]; then
    echo -e "${GREEN}✓ Core submodule is already up to date${NC}"
    cd "$REPO_DIR"
    exit 0
fi

# Show what will be updated
echo ""
echo "Current core commit: ${CURRENT_COMMIT:0:8}"
echo "Latest core commit:  ${REMOTE_COMMIT:0:8}"
echo ""
git log --oneline --graph "$CURRENT_COMMIT..$REMOTE_COMMIT"
echo ""

# Update to latest
git checkout main
git pull origin main

echo -e "${GREEN}✓ Core submodule updated${NC}"
cd "$REPO_DIR"

# Stage the submodule update
git add core

echo ""
echo -e "${GREEN}✓ Submodule update staged${NC}"
echo ""
echo "Next steps:"
echo "  1. Review the changes: git diff --cached"
echo "  2. Commit the update: git commit -m 'Update core submodule to latest'"
echo "  3. Push to remote: git push"
echo ""
echo "Or use: ./scripts/git-commit-and-push.sh"
