import numpy as np
import math
import matplotlib.pyplot as plt

def outlier_filter(datas, threshold = 2):
    datas = np.array(datas).astype(np.int_)
    if (math.isclose(0, datas.std(), rel_tol = 1e-10)):
        return datas
    z = np.abs((datas - datas.mean()) / datas.std())
    return datas[z < threshold]

def data_processing(datas):
    for i in range(datas.shape[0]-1): 
        if i == 0:
            final_arr = outlier_filter(datas[i]).mean()
        else:
            final_arr = np.append(final_arr,outlier_filter(datas[i]).mean())
    return final_arr

if __name__ == "__main__":
    # txt to numpy array and detect outlier
    klines = []
    ulines = []
    k2ulines = []
    totallines = []
    with open("ktime.txt") as textFile:
        klines = [line.split() for line in textFile]
    with open("utime.txt") as textFile:
        ulines = [line.split() for line in textFile]
    with open("k_to_utime.txt") as textFile:
        k2ulines = [line.split() for line in textFile]
    with open("total_time.txt") as textFile:
        totallines = [line.split() for line in textFile]
    fp = open("plot.txt","w")
    klines = data_processing(np.array(klines))
    ulines = data_processing(np.array(ulines))
    k2ulines = data_processing(np.array(k2ulines))
    totallines =  data_processing(np.array(totallines))
    num = np.array([x for x in range(1,1000)])
    # plot
    plt.title("execution time")
    plt.xlabel("n")
    plt.ylabel("ns")
    plt.plot(num, klines)
    plt.plot(num, k2ulines)
    plt.plot(num, ulines)
    plt.plot(num, totallines)
    plt.legend(['kernel_time', 'copy to user time', 'user time', 'total_time'])
    plt.show()
    pngName = "execution_time.png"
    plt.savefig(pngName)
    plt.close()
