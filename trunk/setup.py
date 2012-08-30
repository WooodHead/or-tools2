from setuptools import setup
from os.path import join as pjoin
from glob import glob as gg

setup(
    name='or-tools',
    version='1.0',
    install_requires = ['google-apputils >= 0.3'],
    dependency_links = ['http://google-apputils-python.googlecode.com/files/'],
    packages=[
        pjoin('src', 'constraint_solver'),
        pjoin('src', 'gen', 'constraint_solver'),
        pjoin('src', 'linear_solver'),
        pjoin('src', 'gen', 'linear_solver'),
        pjoin('src', 'graph'),
        pjoin('src', 'gen', 'graph'),
        pjoin('src', 'algorithms'),
        pjoin('src', 'gen', 'algorithms'),
        ],
    data_files=[('lib', [
                pjoin('lib', '_pywrapcp.so'),
                pjoin('lib', '_pywraprouting.so'),
                pjoin('lib', '_pywraplp.so'),
                pjoin('lib', '_pywrapknapsack_solver.so'),
                pjoin('lib', '_pywrapgraph.so'),
                ]),
                ('or-tools', ['LICENSE-2.0.txt',]),
                (pjoin('or-tools', 'examples', 'python'),
                 gg(pjoin('examples', 'python', '*.py'))),
                (pjoin('or-tools', 'data', 'bacp'),
                 gg(pjoin('data', 'bacp', '*'))),
                (pjoin('or-tools', 'data', 'cvrptw'),
                 gg(pjoin('data', 'cvrptw', '*'))),
                (pjoin('or-tools', 'data', 'discrete_tomography'),
                 gg(pjoin('data', 'discrete_tomography', '*'))),
                (pjoin('or-tools', 'data', 'et_jobshop', 'jet'),
                 gg(pjoin('data', 'et_jobshop', 'jet', '*'))),
                (pjoin('or-tools', 'data', 'et_jobshop', 'dyn'),
                 gg(pjoin('data', 'et_jobshop', 'dyn', '*'))),
                (pjoin('or-tools', 'data', 'fill_a_pix'),
                 gg(pjoin('data', 'fill_a_pix', '*'))),
                (pjoin('or-tools', 'data', 'jobshop'),
                 gg(pjoin('data', 'jobshop', '*'))),
                (pjoin('or-tools', 'data', 'minesweeper'),
                 gg(pjoin('data', 'minesweeper', '*'))),
                (pjoin('or-tools', 'data', 'multidim_knapsack'),
                 gg(pjoin('data', 'multidim_knapsack', '*'))),
                (pjoin('or-tools', 'data', 'nonogram_regular'),
                 gg(pjoin('data', 'nonogram_regular', '*'))),
                (pjoin('or-tools', 'data', 'pdptw'),
                 gg(pjoin('data', 'pdptw', '*'))),
                (pjoin('or-tools', 'data', 'quasigroup_completion'),
                 gg(pjoin('data', 'quasigroup_completion', '*'))),
                (pjoin('or-tools', 'data', 'rogo'),
                 gg(pjoin('data', 'rogo', '*'))),
                (pjoin('or-tools', 'data', 'steel_mill'),
                 gg(pjoin('data', 'steel_mill', '*'))),
                (pjoin('or-tools', 'data', 'strimko'),
                 gg(pjoin('data', 'strimko', '*'))),
                (pjoin('or-tools', 'data', 'survo_puzzleX'),
                 gg(pjoin('data', 'survo_puzzle', '*'))),
                ],
    license='Apache 2.0',
    author = "Google Inc",
    author_email = "lperron@google.com",
    description = "Google OR-Tools",
    keywords = ("operations research, constraint programming, " +
                "linear programming"),
    url = "http://code.google.com/p/or-tools/",
)
