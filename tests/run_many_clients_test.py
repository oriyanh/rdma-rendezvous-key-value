import subprocess
import os
import signal
import time
# List of client commands to run in parallel
def single_test():
    client_commands = [
        "./client mlx-stud-01 inputs/inputfile1.txt",
        "./client mlx-stud-01 inputs/inputfile2.txt",
        "./client mlx-stud-01 inputs/inputfile3.txt",
        "./client mlx-stud-01 inputs/inputfile4.txt"
    ]

    # Start the client commands in parallel
    client_processes = []
    for command in client_commands:
        process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        client_processes.append(process)

    print("Waiting for clients to start...")
    time.sleep(5)
    # Start the server command without waiting for it to finish and without capturing its output
    server_command = "./server"
    server_process = subprocess.Popen(server_command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    test_failed = False
    # Wait for all client processes to complete
    for i, process in enumerate(client_processes):
        stdout, stderr = process.communicate()  # Wait for the client process to finish
        if "message received" not in stdout.decode():
            print(f"Client command finished with return code {process.returncode}")
            print("Problem with client command: ")
            print(f"STDOUT: {stdout.decode()}")
            print(f"STDERR: {stderr.decode()}")
            test_failed = True
        else:
            print(f"Client command {i+1} finished successfully")
            
        
            
    # Kill the server process after all clients are done
    server_process.terminate()
    server_process.wait()  # Wait for the server process to terminate
    
    return test_failed
    
if __name__ == "__main__":
    for i in range(100):
        print(f"Test {i+1}")
        if single_test():
            break
            
