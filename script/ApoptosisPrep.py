# usage:
# first_update last_update h5_filenames

import numpy as np
import h5py
import sys
import os
from tqdm import tqdm
import pandas as pd

first_update = int(sys.argv[1])
last_update = int(sys.argv[2])
filenames = sys.argv[3:]

def FracApoptosis(file):
    return np.mean([
        1 if apop[idx] else 0 # 1 and 2 are apoptosis types
        for apop in [
            np.array(
                file['Apoptosis']['upd_'+str(upd)]
            ).flatten()
            for upd in range(first_update, last_update)
        ]
        for idx in range(file['Index']['own'].size)
    ])

def ExtractTreat(filename):
    return next(
        str for str in os.path.basename(filename).split('+') if "treat=" in str
    )

print("num files:" , len(filenames))

outfile = 'script_hash=TODO~source_hash=TODO~emp_hash=TODO~title=apoptosis.csv'

pd.DataFrame.from_dict([
    {
        'Treatment' : treat,
        'Per-Cell-Update Apoptosis Rate' : FracApoptosis(file),
        'First Update' : first_update,
        'Last Update' : last_update
    }
    for treat, file in (
        (ExtractTreat(filename), h5py.File(filename, 'r')) for filename in tqdm(filenames)
    )
]).to_csv(outfile, index=False)

print('Output saved to', outfile)