
from importers.maps import diff_maps
from importers.starters import diff_starters
from importers.species import diff_species
import os
import filecmp
from constants import Constants

def detect_engine_changes(vanilla, source, report):
    for d in ["src", "include"]:
        v_dir = vanilla.get_file(d)
        s_dir = source.get_file(d)
        if not os.path.exists(v_dir) or not os.path.exists(s_dir):
            continue
            
        for root, dirs, files in os.walk(s_dir):
            for file in files:
                if not (file.endswith(".c") or file.endswith(".h")):
                    continue
                s_path = os.path.join(root, file)
                rel_path = os.path.relpath(s_path, s_dir)
                v_path = os.path.join(v_dir, rel_path)
                
                if d == "src" and "data" in rel_path:
                    continue
                    
                if not os.path.exists(v_path):
                    report.add_unsupported("custom_code", os.path.join(d, rel_path), "New engine file")
                elif not filecmp.cmp(v_path, s_path, shallow=False):
                    report.add_unsupported("custom_code", os.path.join(d, rel_path), "Modified engine file")

def compare_projects(vanilla, source, report):
    mod_data = {}
    
    detect_engine_changes(vanilla, source, report)
    
    consts = Constants(source)
    
    map_data, script_data = diff_maps(vanilla, source, consts, report)
    if map_data:
        mod_data['maps'] = map_data
    if script_data:
        mod_data['scripts'] = script_data
        
    starters_data = diff_starters(vanilla, source, consts, report)
    if starters_data:
        mod_data["starters"] = starters_data
        
    species_data = diff_species(vanilla, source, consts, report)
    if species_data:
        mod_data["species"] = species_data
        
    return mod_data

