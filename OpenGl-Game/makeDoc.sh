#!/bin/bash

# makeDoc.sh - Script to generate Doxygen documentation for the OpenGL Game project

echo "Generating Doxygen documentation..."

# Check if doxygen is installed
if ! command -v doxygen &> /dev/null; then
    echo "Error: Doxygen is not installed. Please install Doxygen first."
    echo "On Ubuntu/Debian: sudo apt-get install doxygen"
    echo "On macOS: brew install doxygen"
    exit 1
fi

# Create Doxyfile if it doesn't exist
if [ ! -f "Doxyfile" ]; then
    echo "Doxyfile not found. Creating a basic configuration..."
    doxygen -g Doxyfile
    
    # Configure Doxyfile for this project
    sed -i 's/^PROJECT_NAME.*$/PROJECT_NAME = "OpenGL Game"/' Doxyfile
    sed -i 's/^PROJECT_BRIEF.*$/PROJECT_BRIEF = "A simple 3D OpenGL game with camera controls and collectibles"/' Doxyfile
    sed -i 's/^INPUT.*$/INPUT = src/' Doxyfile
    sed -i 's/^RECURSIVE.*$/RECURSIVE = YES/' Doxyfile
    sed -i 's/^EXCLUDE.*$/EXCLUDE = include/' Doxyfile
    sed -i 's/^EXCLUDE_PATTERNS.*$/EXCLUDE_PATTERNS = *glad* */' Doxyfile
    sed -i 's/^OUTPUT_DIRECTORY.*$/OUTPUT_DIRECTORY = docs/' Doxyfile
    sed -i 's/^GENERATE_HTML.*$/GENERATE_HTML = YES/' Doxyfile
    sed -i 's/^GENERATE_LATEX.*$/GENERATE_LATEX = NO/' Doxyfile
    sed -i 's/^HTML_OUTPUT.*$/HTML_OUTPUT = html/' Doxyfile
    sed -i 's/^HTML_COLORSTYLE.*$/HTML_COLORSTYLE = AUTO/' Doxyfile
    sed -i 's/^HTML_TIMESTAMP.*$/HTML_TIMESTAMP = YES/' Doxyfile
    sed -i 's/^EXTRACT_ALL.*$/EXTRACT_ALL = YES/' Doxyfile
    sed -i 's/^EXTRACT_PRIVATE.*$/EXTRACT_PRIVATE = NO/' Doxyfile
    sed -i 's/^EXTRACT_STATIC.*$/EXTRACT_STATIC = NO/' Doxyfile
    sed -i 's/^SOURCE_BROWSER.*$/SOURCE_BROWSER = YES/' Doxyfile
    sed -i 's/^INLINE_SOURCES.*$/INLINE_SOURCES = YES/' Doxyfile
    sed -i 's/^HAVE_DOT.*$/HAVE_DOT = NO/' Doxyfile
    sed -i 's/^CALL_GRAPH.*$/CALL_GRAPH = NO/' Doxyfile
    sed -i 's/^CALLER_GRAPH.*$/CALLER_GRAPH = NO/' Doxyfile
    sed -i 's/^OPTIMIZE_OUTPUT_FOR_C.*$/OPTIMIZE_OUTPUT_FOR_C = NO/' Doxyfile
    sed -i 's/^OPTIMIZE_OUTPUT_JAVA.*$/OPTIMIZE_OUTPUT_JAVA = NO/' Doxyfile
    sed -i 's/^OPTIMIZE_FOR_FORTRAN.*$/OPTIMIZE_FOR_FORTRAN = NO/' Doxyfile
    sed -i 's/^OPTIMIZE_OUTPUT_FOR_CXX.*$/OPTIMIZE_OUTPUT_FOR_CXX = YES/' Doxyfile
    sed -i 's/^BUILTIN_STL_SUPPORT.*$/BUILTIN_STL_SUPPORT = YES/' Doxyfile
    sed -i 's/^CPP_CLI_SUPPORT.*$/CPP_CLI_SUPPORT = NO/' Doxyfile
    sed -i 's/^SIP_SUPPORT.*$/SIP_SUPPORT = NO/' Doxyfile
    sed -i 's/^IDL_PROPERTY_SUPPORT.*$/IDL_PROPERTY_SUPPORT = NO/' Doxyfile
    sed -i 's/^DISTRIBUTE_GROUP_DOC.*$/DISTRIBUTE_GROUP_DOC = NO/' Doxyfile
    sed -i 's/^SUBGROUPING.*$/SUBGROUPING = YES/' Doxyfile
    sed -i 's/^INLINE_GROUPED_CLASSES.*$/INLINE_GROUPED_CLASSES = NO/' Doxyfile
    sed -i 's/^INLINE_SIMPLE_STRUCTS.*$/INLINE_SIMPLE_STRUCTS = NO/' Doxyfile
    sed -i 's/^TYPEDEF_HIDES_STRUCT.*$/TYPEDEF_HIDES_STRUCT = NO/' Doxyfile
    sed -i 's/^LOOKUP_CACHE_SIZE.*$/LOOKUP_CACHE_SIZE = 0/' Doxyfile
fi

# Create docs directory if it doesn't exist
mkdir -p docs

# Generate documentation
echo "Running Doxygen..."
doxygen Doxyfile

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
