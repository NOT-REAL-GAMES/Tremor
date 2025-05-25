# -*- coding: utf-8 -*-
"""
Encoder.

It encodes.
"""

import os
import shutil

def fix_unicode_file(filepath):
    """Fix Unicode issues in C++ source files"""
    
    # Create backup first
    backup_path = filepath + ".backup"
    shutil.copy2(filepath, backup_path)
    print(f"Created backup: {backup_path}")
    
    try:
        # Read file with error handling
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        
        # Clean common Unicode issues
        content = content.replace('\u2019', "'")  # Right single quotation mark
        content = content.replace('\u2018', "'")  # Left single quotation mark  
        content = content.replace('\u201c', '"')  # Left double quotation mark
        content = content.replace('\u201d', '"')  # Right double quotation mark
        content = content.replace('\u2013', '-')  # En dash
        content = content.replace('\u2014', '--') # Em dash
        content = content.replace('\u00a0', ' ')  # Non-breaking space
        content = content.replace('\u2026', '...') # Horizontal ellipsis
        content = content.replace('\u2713', '[X]')  # Check mark → [X]
        
        # Write back as clean ASCII/UTF-8
        with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
            
        print(f"Fixed Unicode issues in: {filepath}")
        return True
        
    except Exception as e:
        # Restore backup if something goes wrong
        shutil.copy2(backup_path, filepath)
        print(f"Error fixing file, restored backup: {e}")
        return False

def fix_project_files(project_dir):
    """Fix Unicode issues in all C++ files in a project"""
    
    cpp_extensions = ['.cpp', '.h', '.hpp', '.cc', '.cxx']
    fixed_count = 0
    
    for root, dirs, files in os.walk(project_dir):
        for file in files:
            if any(file.endswith(ext) for ext in cpp_extensions):
                filepath = os.path.join(root, file)
                print(f"Processing: {filepath}")
                
                if fix_unicode_file(filepath):
                    fixed_count += 1
    
    print(f"\nFixed {fixed_count} files total")

# Usage examples:

# Fix single file
fix_unicode_file(r"C:\Projects\Tremor\taffy\taffy.h")

# Or fix entire project
# fix_project_files(r"C:\Projects\Tremor")

# Alternative: Just specify encoding when opening files
def safe_read_cpp_file(filepath):
    """Safely read C++ file with multiple encoding attempts"""
    encodings = ['utf-8', 'windows-1252', 'iso-8859-1', 'ascii']
    
    for encoding in encodings:
        try:
            with open(filepath, 'r', encoding=encoding) as f:
                content = f.read()
            print(f"Successfully read with {encoding} encoding")
            return content
        except UnicodeDecodeError:
            continue
    
    # Fallback: read with error handling
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()
    print("Used UTF-8 with error replacement")
    return content

# For reading problematic files:
# content = safe_read_cpp_file(r"C:\Projects\Tremor\vk.cpp")