#!/bin/bash

# makeDoc.sh - Generate Doxygen documentation for WEXYRON.
# The Doxyfile lives next to this script; output goes to docs/html.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
DOXYFILE="$SCRIPT_DIR/Doxyfile"

cd "$REPO_ROOT"

echo "Generating Doxygen documentation..."

# Check if doxygen is installed
if ! command -v doxygen &> /dev/null; then
    echo "Error: Doxygen is not installed. Please install Doxygen first."
    echo "On Ubuntu/Debian: sudo apt-get install doxygen"
    echo "On macOS: brew install doxygen"
    exit 1
fi

# Create Doxyfile if it doesn't exist
if [ ! -f "$DOXYFILE" ]; then
    echo "Doxyfile not found. Creating a basic configuration..."
    doxygen -g "$DOXYFILE"

    # Configure Doxyfile for this project
    sed -i 's/^PROJECT_NAME.*$/PROJECT_NAME = "WEXYRON"/' "$DOXYFILE"
    sed -i 's/^PROJECT_BRIEF.*$/PROJECT_BRIEF = "A multiplayer 3D first-person shooter built on OpenGL"/' "$DOXYFILE"
    sed -i 's/^INPUT .*$/INPUT = src engine/' "$DOXYFILE"
    sed -i 's/^RECURSIVE.*$/RECURSIVE = YES/' "$DOXYFILE"
    sed -i 's/^EXCLUDE .*$/EXCLUDE = include third_party build build-cmake/' "$DOXYFILE"
    sed -i 's/^EXCLUDE_PATTERNS.*$/EXCLUDE_PATTERNS = *glad* */' "$DOXYFILE"
    sed -i 's/^OUTPUT_DIRECTORY.*$/OUTPUT_DIRECTORY = docs/' "$DOXYFILE"
    sed -i 's/^GENERATE_HTML.*$/GENERATE_HTML = YES/' "$DOXYFILE"
    sed -i 's/^GENERATE_LATEX.*$/GENERATE_LATEX = NO/' "$DOXYFILE"
    sed -i 's/^HTML_OUTPUT.*$/HTML_OUTPUT = html/' "$DOXYFILE"
    sed -i 's/^HTML_COLORSTYLE.*$/HTML_COLORSTYLE = AUTO/' "$DOXYFILE"
    sed -i 's/^HTML_TIMESTAMP.*$/HTML_TIMESTAMP = YES/' "$DOXYFILE"
    sed -i 's/^EXTRACT_ALL.*$/EXTRACT_ALL = YES/' "$DOXYFILE"
    sed -i 's/^EXTRACT_PRIVATE.*$/EXTRACT_PRIVATE = NO/' "$DOXYFILE"
    sed -i 's/^EXTRACT_STATIC.*$/EXTRACT_STATIC = NO/' "$DOXYFILE"
    sed -i 's/^SOURCE_BROWSER.*$/SOURCE_BROWSER = YES/' "$DOXYFILE"
    sed -i 's/^INLINE_SOURCES.*$/INLINE_SOURCES = YES/' "$DOXYFILE"
    sed -i 's/^HAVE_DOT.*$/HAVE_DOT = NO/' "$DOXYFILE"
    sed -i 's/^CALL_GRAPH.*$/CALL_GRAPH = NO/' "$DOXYFILE"
    sed -i 's/^CALLER_GRAPH.*$/CALLER_GRAPH = NO/' "$DOXYFILE"
    sed -i 's/^OPTIMIZE_OUTPUT_FOR_C.*$/OPTIMIZE_OUTPUT_FOR_C = NO/' "$DOXYFILE"
    sed -i 's/^OPTIMIZE_OUTPUT_JAVA.*$/OPTIMIZE_OUTPUT_JAVA = NO/' "$DOXYFILE"
    sed -i 's/^OPTIMIZE_FOR_FORTRAN.*$/OPTIMIZE_FOR_FORTRAN = NO/' "$DOXYFILE"
    sed -i 's/^OPTIMIZE_OUTPUT_FOR_CXX.*$/OPTIMIZE_OUTPUT_FOR_CXX = YES/' "$DOXYFILE"
    sed -i 's/^BUILTIN_STL_SUPPORT.*$/BUILTIN_STL_SUPPORT = YES/' "$DOXYFILE"
    sed -i 's/^CPP_CLI_SUPPORT.*$/CPP_CLI_SUPPORT = NO/' "$DOXYFILE"
    sed -i 's/^SIP_SUPPORT.*$/SIP_SUPPORT = NO/' "$DOXYFILE"
    sed -i 's/^IDL_PROPERTY_SUPPORT.*$/IDL_PROPERTY_SUPPORT = NO/' "$DOXYFILE"
    sed -i 's/^DISTRIBUTE_GROUP_DOC.*$/DISTRIBUTE_GROUP_DOC = NO/' "$DOXYFILE"
    sed -i 's/^SUBGROUPING.*$/SUBGROUPING = YES/' "$DOXYFILE"
    sed -i 's/^INLINE_GROUPED_CLASSES.*$/INLINE_GROUPED_CLASSES = NO/' "$DOXYFILE"
    sed -i 's/^INLINE_SIMPLE_STRUCTS.*$/INLINE_SIMPLE_STRUCTS = NO/' "$DOXYFILE"
    sed -i 's/^TYPEDEF_HIDES_STRUCT.*$/TYPEDEF_HIDES_STRUCT = NO/' "$DOXYFILE"
    sed -i 's/^LOOKUP_CACHE_SIZE.*$/LOOKUP_CACHE_SIZE = 0/' "$DOXYFILE"
fi

# Create docs directory if it doesn't exist
mkdir -p docs

# Generate documentation
echo "Running Doxygen..."
doxygen "$DOXYFILE"

# Check if documentation was generated successfully
if [ -f "docs/html/index.html" ]; then
    echo "Documentation generated successfully!"
    echo "Open docs/html/index.html in your browser to view the documentation."
    echo ""
    echo "Documentation location: $(pwd)/docs/html/"
    echo "Main page: $(pwd)/docs/html/index.html"
else
    echo "Error: Documentation generation failed. Check the Doxyfile configuration."
    exit 1
fi

echo "Done!"
