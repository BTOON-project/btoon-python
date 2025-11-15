#!/bin/bash
# Commit and push btoon-python changes including submodule updates

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

echo -e "${BLUE}📝 Committing and pushing btoon-python changes...${NC}"
echo ""

# Check if there are changes to commit
if git diff-index --quiet HEAD --; then
    echo -e "${YELLOW}No changes to commit${NC}"
    exit 0
fi

# Show status
echo "Current status:"
git status --short
echo ""

# Check if submodule is updated
if git diff --cached --name-only | grep -q "^core$"; then
    cd core
    CORE_COMMIT=$(git rev-parse HEAD)
    cd "$REPO_DIR"
    echo -e "${GREEN}Core submodule will be updated to: ${CORE_COMMIT:0:8}${NC}"
    echo ""
fi

# Get commit message
if [ -z "$1" ]; then
    echo "Enter commit message (or press Ctrl+C to cancel):"
    read -r COMMIT_MSG
else
    COMMIT_MSG="$1"
fi

if [ -z "$COMMIT_MSG" ]; then
    echo -e "${RED}Commit message cannot be empty${NC}"
    exit 1
fi

# Commit
echo ""
echo "Committing with message: $COMMIT_MSG"
git commit -m "$COMMIT_MSG"

echo -e "${GREEN}✓ Changes committed${NC}"
echo ""

# Ask to push
read -p "Push to remote? (Y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    echo "Pushing to origin..."
    git push
    echo -e "${GREEN}✓ Changes pushed to remote${NC}"
else
    echo -e "${YELLOW}Skipped push. Run 'git push' manually when ready.${NC}"
fi

echo ""
echo -e "${GREEN}✓ Done!${NC}"
