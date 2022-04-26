class time:
    def __init__(self, hora=0, min=0, seg=0):
        self.hora = hora
        self.min = min
        self.seg = seg
    def from_string(self, text):
        '''Sets the values passing a string hh:mm:ss as text'''
        self.hora = int(text[0:2])
        self.min = int(text[3:5])
        self.seg = int(text[6:8])
    def __str__(self):
        return f'{self.hora:02d}:{self.min:02d}:{self.seg:02d}'

class feed:
    __inicio: time
    #__inicio = time()
    __duracao: int
    #__duracao = 0

    def __init__(self, inicio: time, duracao: int):
        self.__inicio = inicio
        self.__duracao = duracao

    def get_inicio(self):
        return self.__inicio

    def get_duracao(self):
        return self.__duracao

    def __str__(self):
        return f'Inicio: {self.get_inicio().__str__()} Duracao: {self.get_duracao()}'

        