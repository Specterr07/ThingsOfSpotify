import spotipy
from spotipy.oauth2 import SpotifyOAuth
from flask import Flask, request, redirect, jsonify
from functools import wraps
import os
import time
import logging
from datetime import datetime, timedelta

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('spotify_server.log'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)

# Set up Spotify credentials
os.environ["SPOTIPY_CLIENT_ID"] = ""
os.environ["SPOTIPY_CLIENT_SECRET"] = ""
os.environ["SPOTIPY_REDIRECT_URI"] = "http://localhost:8080/callback"

# Enhanced scope for more features
scope = (
    "user-modify-playback-state "
    "user-read-playback-state "
    "user-read-currently-playing "
    "user-read-recently-played "
    "playlist-read-private "
    "playlist-read-collaborative"
)

app = Flask(__name__)

# Configuration
app.config.update(
    SESSION_COOKIE_SECURE=True,
    SESSION_COOKIE_HTTPONLY=True,
    SESSION_COOKIE_SAMESITE='Lax',
)

# Global variables
sp_oauth = SpotifyOAuth(scope=scope)
sp = None
last_token_refresh = None
playback_cache = {
    'data': None,
    'timestamp': None
}

def require_auth(f):
    """Decorator to ensure Spotify client is authenticated"""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        global sp, last_token_refresh
        
        try:
            # Check if token needs refresh (every 30 minutes)
            if not last_token_refresh or time.time() - last_token_refresh > 1800:
                token_info = sp_oauth.get_cached_token()
                if token_info and sp_oauth.is_token_expired(token_info):
                    token_info = sp_oauth.refresh_access_token(token_info['refresh_token'])
                    sp = spotipy.Spotify(auth=token_info['access_token'])
                    last_token_refresh = time.time()
                    logger.info("Token refreshed successfully")
            
            if not sp:
                logger.error("Spotify client not initialized")
                return jsonify({'error': 'Not authenticated'}), 401
                
            return f(*args, **kwargs)
            
        except Exception as e:
            logger.error(f"Authentication error: {str(e)}")
            return jsonify({'error': 'Authentication failed'}), 401
            
    return decorated_function

def cache_playback(timeout=1):
    """Decorator to cache playback status"""
    def decorator(f):
        @wraps(f)
        def decorated_function(*args, **kwargs):
            global playback_cache
            
            if (playback_cache['data'] is None or 
                playback_cache['timestamp'] is None or 
                time.time() - playback_cache['timestamp'] > timeout):
                
                result = f(*args, **kwargs)
                playback_cache['data'] = result
                playback_cache['timestamp'] = time.time()
                return result
            
            return playback_cache['data']
            
        return decorated_function
    return decorator

@app.route('/')
def home():
    """Home route with authentication status"""
    token_info = sp_oauth.get_cached_token()
    if not token_info:
        auth_url = sp_oauth.get_authorize_url()
        return redirect(auth_url)
    else:
        global sp
        sp = spotipy.Spotify(auth_manager=sp_oauth)
        return jsonify({
            'status': 'authenticated',
            'message': 'Spotify Authorization Complete. Use control endpoints.'
        })

@app.route('/callback')
def callback():
    """OAuth callback handler"""
    try:
        code = request.args.get("code")
        token_info = sp_oauth.get_access_token(code)
        global sp, last_token_refresh
        sp = spotipy.Spotify(auth=token_info['access_token'])
        last_token_refresh = time.time()
        return redirect('/')
    except Exception as e:
        logger.error(f"Callback error: {str(e)}")
        return jsonify({'error': 'Authentication failed'}), 401

@app.route('/spotify/play', methods=['GET'])
@require_auth
def play():
    """Start or resume playback"""
    try:
        sp.start_playback()
        return jsonify({'status': 'success', 'message': 'Playing music'})
    except Exception as e:
        logger.error(f"Play error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/spotify/pause', methods=['GET'])
@require_auth
def pause():
    """Pause playback"""
    try:
        sp.pause_playback()
        return jsonify({'status': 'success', 'message': 'Paused music'})
    except Exception as e:
        logger.error(f"Pause error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/spotify/next', methods=['GET'])
@require_auth
def next_track():
    """Skip to next track"""
    try:
        sp.next_track()
        return jsonify({'status': 'success', 'message': 'Skipped to next track'})
    except Exception as e:
        logger.error(f"Next track error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/spotify/previous', methods=['GET'])
@require_auth
def previous_track():
    """Go to previous track"""
    try:
        sp.previous_track()
        return jsonify({'status': 'success', 'message': 'Went to previous track'})
    except Exception as e:
        logger.error(f"Previous track error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/spotify/volume/<int:volume>', methods=['GET'])
@require_auth
def set_volume(volume):
    """Set playback volume"""
    try:
        volume = max(0, min(100, volume))
        sp.volume(volume)
        return jsonify({
            'status': 'success',
            'message': f'Volume set to {volume}%',
            'volume': volume
        })
    except Exception as e:
        logger.error(f"Volume error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/spotify/seek/<int:position>', methods=['GET'])
@require_auth
def seek_position(position):
    """Seek to position in track"""
    try:
        sp.seek_track(position)
        return jsonify({
            'status': 'success',
            'message': f'Seeked to position {position}ms',
            'position': position
        })
    except Exception as e:
        logger.error(f"Seek error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/spotify/status', methods=['GET'])
@require_auth
@cache_playback(timeout=1)
def get_status():
    """Get detailed playback status"""
    try:
        current_playback = sp.current_playback()
        if current_playback:
            # Get additional track information
            track = current_playback['item']
            artists = ", ".join([artist['name'] for artist in track['artists']])
            
            # Calculate progress percentage
            progress_ms = current_playback['progress_ms']
            duration_ms = track['duration_ms']
            progress_percent = (progress_ms / duration_ms) * 100 if duration_ms > 0 else 0
            
            return jsonify({
                'is_playing': current_playback['is_playing'],
                'volume': current_playback['device']['volume_percent'],
                'track': track['name'],
                'artist': artists,
                'album': track['album']['name'],
                'duration_ms': duration_ms,
                'position_ms': progress_ms,
                'progress_percent': progress_percent,
                'shuffle_state': current_playback['shuffle_state'],
                'repeat_state': current_playback['repeat_state'],
                'device': {
                    'name': current_playback['device']['name'],
                    'type': current_playback['device']['type']
                },
                'timestamp': int(time.time() * 1000)
            })
        return jsonify({'error': 'No active playback'}), 204
    except Exception as e:
        logger.error(f"Status error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/spotify/devices', methods=['GET'])
@require_auth
def get_devices():
    """Get available playback devices"""
    try:
        devices = sp.devices()
        return jsonify({
            'status': 'success',
            'devices': devices['devices']
        })
    except Exception as e:
        logger.error(f"Devices error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.errorhandler(404)
def not_found_error(error):
    """Handle 404 errors"""
    return jsonify({'error': 'Not found'}), 404

@app.errorhandler(500)
def internal_error(error):
    """Handle 500 errors"""
    logger.error(f"Internal server error: {str(error)}")
    return jsonify({'error': 'Internal server error'}), 500

if __name__ == '__main__':
    # Add SSL context for development
    ssl_context = None
    if os.path.exists('cert.pem') and os.path.exists('key.pem'):
        ssl_context = ('cert.pem', 'key.pem')
    
    app.run(
        host='0.0.0.0',
        port=8080,
        ssl_context=ssl_context,
        debug=False  # Set to False in production
    )





