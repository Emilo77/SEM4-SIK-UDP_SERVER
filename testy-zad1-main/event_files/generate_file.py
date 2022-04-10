import os

def generate_file(generator):
    output_dir = 'event_files/generated/'
    filename = output_dir + generator.__name__
    # if os.path.exists(filename):
        # return filename
    if not os.path.exists(output_dir):
        os.mkdir(output_dir)
    generator(open(filename, 'w'))
    return filename
