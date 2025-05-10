from flask import Flask, request, render_template, jsonify
import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestRegressor, GradientBoostingRegressor
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import mean_squared_error

app = Flask(__name__)

# Load and process JSON data
data = pd.read_json('bin_data.json')
if 'date' not in data.columns:
    data['date'] = pd.date_range(start='2025-01-01', periods=len(data))
data['date'] = pd.to_datetime(data['date'])
data['day_of_week'] = data['date'].dt.dayofweek

# Aggregating daily data into weekly data
weekly_data = data.resample('W', on='date').agg({
    'bin_level': 'mean',
    'odor_level': 'max',
    'bin_full_flag': 'sum'
}).reset_index()
weekly_data.rename(columns={'bin_full_flag': 'weekly_cleanings'}, inplace=True)

# Adding interaction feature to enhance feature significance
weekly_data['interaction'] = weekly_data['bin_level'] * weekly_data['odor_level']

X = weekly_data[['bin_level', 'odor_level', 'interaction']]
y_cleanings = weekly_data['weekly_cleanings']

# Standardizing features
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# Train-test split for both models
X_train, X_test, y_train_cleanings, y_test_cleanings = train_test_split(X_scaled, y_cleanings, test_size=0.2, random_state=42)

# Initialize and train the Random Forest Regressor (for weekly cleanings)
rf_model = RandomForestRegressor(n_estimators=200, max_depth=10, random_state=42)
rf_model.fit(X_train, y_train_cleanings)

# Preparing time-based data for Gradient Boosting Regressor (days until next cleaning)
daily_data = data[['date', 'bin_level', 'odor_level', 'bin_full_flag']].copy()
daily_data['days_since_last_clean'] = daily_data['bin_full_flag'].cumsum().shift(fill_value=0)
X_time = daily_data[['bin_level', 'odor_level', 'days_since_last_clean']]
y_time = daily_data['days_since_last_clean'].shift(-1, fill_value=0)

X_train_time, X_test_time, y_train_time, y_test_time = train_test_split(X_time, y_time, test_size=0.2, random_state=42)
gb_model = GradientBoostingRegressor(n_estimators=100, max_depth=5, learning_rate=0.1, random_state=42)
gb_model.fit(X_train_time, y_train_time)

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/predict', methods=['POST'])
def predict():
    data = request.get_json()  # Parse JSON request body
    bin_level = float(data['bin_level'])
    odor_level = float(data['odor_level'])

    # Adding interaction feature and scaling input for weekly cleanings prediction
    interaction = bin_level * odor_level
    new_input = pd.DataFrame({'bin_level': [bin_level], 'odor_level': [odor_level], 'interaction': [interaction]})
    new_input_scaled = scaler.transform(new_input)
    weekly_cleanings_pred = rf_model.predict(new_input_scaled)

    # Predicting days until next cleaning using Gradient Boosting Regressor
    new_input_time = pd.DataFrame({'bin_level': [bin_level], 'odor_level': [odor_level], 'days_since_last_clean': [0]})
    days_until_cleaning_pred = gb_model.predict(new_input_time)

    return jsonify(
        predicted_cleanings=round(weekly_cleanings_pred[0], 2),
        days_until_next_cleaning=round(days_until_cleaning_pred[0], 2)
    )

if __name__ == '__main__':
    app.run(debug=True)
