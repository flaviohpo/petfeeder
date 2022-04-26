# import the Flask class from the flask module
from flask import Flask, render_template, request, redirect, Response
from petfeed import time, feed
import yaml

with open("parameters.yaml", 'r') as stream:
    parameters = yaml.load(stream, Loader=yaml.SafeLoader)

feed = []

# create the application object
app = Flask(__name__)

# use decorators to link the function to a url
@app.route('/')
def home():
    return render_template('home.html', text1=str(horario_inicio), text2=str(horario_fim))  # render a template

@app.route('/set_value', methods=['POST',])
def set_value():
    '''Utilizado pelo browser'''
    texto_horario_inicio = request.form['horario_inicio']
    texto_horario_fim = request.form['horario_fim']
    horario_inicio.from_string(texto_horario_inicio)
    horario_fim.from_string(texto_horario_fim)
    parameters['hora_inicio'] = str(horario_inicio)
    parameters['hora_fim'] = str(horario_fim)
    with open('parameters.yaml', 'w') as outfile:
        yaml.dump(parameters, outfile, default_flow_style=False)
    return ('', 204)

@app.route('/get_value')
def get_value():
    '''Utilizado pelo hardware'''
    resp = Response("horarios")
    resp.headers['horario_inicio'] = str(horario_inicio)
    resp.headers['horario_fim'] = str(horario_fim)
    return resp

# start the server with the 'run()' method
if __name__ == '__main__':
    app.run(debug=True)
