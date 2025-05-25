#!/usr/bin/env python3
"""
Helper script to identify method implementations in C++ headers that need to be moved
"""

import re
import os

def find_implementations_in_header(header_path):
    """Find method implementations in a C++ header file"""
    
    with open(header_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # Patterns to find implementations (methods with bodies)
    patterns = [
        # Constructor implementations: ClassName::ClassName(...) { ... }
        r'(\w+::\w+\([^)]*\)\s*{[^}]*})',
        
        # Method implementations: ReturnType ClassName::methodName(...) { ... }
        r'(\w+\s+\w+::\w+\([^)]*\)\s*{[^}]*})',
        
        # Inline method definitions inside class: type methodName(...) { ... }
        r'(\w+\s+\w+\([^)]*\)\s*{[^{}]*{[^}]*}[^}]*})',
        
        # Static method implementations
        r'(static\s+\w+\s+\w+\([^)]*\)\s*{[^}]*})'
    ]
    
    implementations = []
    
    for pattern in patterns:
        matches = re.finditer(pattern, content, re.MULTILINE | re.DOTALL)
        for match in matches:
            impl = match.group(1)
            # Skip small inline getters (less than 2 lines)
            if impl.count('\n') > 2:
                implementations.append({
                    'text': impl[:100] + '...' if len(impl) > 100 else impl,
                    'line': content[:match.start()].count('\n') + 1,
                    'size': len(impl)
                })
    
    return implementations

def generate_move_checklist(header_path):
    """Generate a checklist of things to move"""
    
    implementations = find_implementations_in_header(header_path)
    
    print(f"Found {len(implementations)} implementations in {header_path}")
    print("=" * 60)
    
    # Sort by size (biggest first - these cause most linker errors)
    implementations.sort(key=lambda x: x['size'], reverse=True)
    
    for i, impl in enumerate(implementations, 1):
        print(f"{i:2d}. Line {impl['line']:4d}: {impl['text']}")
        print(f"    Size: {impl['size']} characters")
        print()
    
    print("\nSUGGESTED ORDER:")
    print("1. Move the largest implementations first")
    print("2. Focus on VulkanClusteredRenderer methods")  
    print("3. Then ShaderReflection methods")
    print("4. Then Buffer and ShaderModule methods")
    print("5. Finally small utility methods")

def create_cpp_template(header_path, cpp_path):
    """Create a template .cpp file"""
    
    header_name = os.path.basename(header_path)
    
    template = f'''#include "{header_name}"

namespace tremor::gfx {{

// TODO: Move all implementations from {header_name} here

// Buffer class implementations
// Buffer::Buffer(...) {{ ... }}
// void Buffer::update(...) {{ ... }}

// ShaderCompiler implementations  
// ShaderCompiler::ShaderCompiler() {{ ... }}
// std::vector<uint32_t> ShaderCompiler::compileToSpv(...) {{ ... }}

// ShaderReflection implementations
// void ShaderReflection::reflect(...) {{ ... }}
// void ShaderReflection::merge(...) {{ ... }}

// ShaderModule implementations
// std::unique_ptr<ShaderModule> ShaderModule::loadFromFile(...) {{ ... }}
// std::unique_ptr<ShaderModule> ShaderModule::compileFromSource(...) {{ ... }}

// VulkanClusteredRenderer implementations
// VulkanClusteredRenderer::VulkanClusteredRenderer(...) {{ ... }}
// bool VulkanClusteredRenderer::initialize(...) {{ ... }}
// void VulkanClusteredRenderer::render(...) {{ ... }}
// ... and ALL other methods

}} // namespace tremor::gfx

// Global function implementations
// void copyBuffer(...) {{ ... }}
'''
    
    with open(cpp_path, 'w') as f:
        f.write(template)
    
    print(f"Created template {cpp_path}")

# Usage:
if __name__ == "__main__":
    header_path = r"C:\Projects\Tremor\taffy\taffy.h"  # Adjust path
    cpp_path = r"C:\Projects\Tremor\vk.cpp"
    
    if os.path.exists(header_path):
        generate_move_checklist(header_path)
        
        if not os.path.exists(cpp_path):
            create_cpp_template(header_path, cpp_path)
    else:
        print(f"Header file not found: {header_path}")
        print("Please adjust the header_path variable")