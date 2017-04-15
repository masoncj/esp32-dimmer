const HtmlWebpackPlugin = require('html-webpack-plugin'); //installed via npm
const ManifestPlugin = require('webpack-manifest-plugin');
const webpack = require('webpack'); //to access built-in plugins

const path = require('path');

const config = {
  entry: './web-src/main.jsx',
  output: {
    path: path.resolve(__dirname, 'web-fs'),
    filename: 'dimmer.js'
  },
  module: {
      loaders: [
        {
          test: /\.jsx$/,
          exclude: /(node_modules|bower_components)/,
          loader: 'babel-loader',
          query: {
            presets: ['es2015']
          }
        }
      ]
  },
  plugins: [
    new webpack.optimize.UglifyJsPlugin(),
    new HtmlWebpackPlugin({template: './web-src/index.html'}),
    new ManifestPlugin()
  ]
};

module.exports = config;
