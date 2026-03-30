#!/bin/bash

echo "Testing shader files..."

# Check if shader files exist
if [ -f "assets/shaders/vertex.glsl" ]; then
    echo "✓ vertex.glsl exists"
    echo "Content:"
    cat assets/shaders/vertex.glsl
else
    echo "✗ vertex.glsl missing"
fi

echo ""

if [ -f "assets/shaders/fragment.glsl" ]; then
    echo "✓ fragment.glsl exists"
    echo "Content:"
    cat assets/shaders/fragment.glsl
else
    echo "✗ fragment.glsl missing"
fi

echo ""

if [ -f "assets/shaders/textured_vertex.glsl" ]; then
    echo "✓ textured_vertex.glsl exists"
    echo "Content:"
    cat assets/shaders/textured_vertex.glsl
else
    echo "✗ textured_vertex.glsl missing"
fi

echo ""

if [ -f "assets/shaders/textured_fragment.glsl" ]; then
    echo "✓ textured_fragment.glsl exists"
    echo "Content:"
    cat assets/shaders/textured_fragment.glsl
else
    echo "✗ textured_fragment.glsl missing"
fi
